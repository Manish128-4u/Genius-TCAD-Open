/********************************************************************************/
/*     888888    888888888   88     888  88888   888      888    88888888       */
/*   8       8   8           8 8     8     8      8        8    8               */
/*  8            8           8  8    8     8      8        8    8               */
/*  8            888888888   8   8   8     8      8        8     8888888        */
/*  8      8888  8           8    8  8     8      8        8            8       */
/*   8       8   8           8     8 8     8      8        8            8       */
/*     888888    888888888  888     88   88888     88888888     88888888        */
/*                                                                              */
/*       A Three-Dimensional General Purpose Semiconductor Simulator.           */
/*                                                                              */
/*                                                                              */
/*  Copyright (C) 2007-2008                                                     */
/*  Cogenda Pte Ltd                                                             */
/*                                                                              */
/*  Please contact Cogenda Pte Ltd for license information                      */
/*                                                                              */
/*  Author: Gong Ding   gdiso@ustc.edu                                          */
/*                                                                              */
/********************************************************************************/



#include "simulation_system.h"
#include "resistance_region.h"
#include "solver_specify.h"
#include "boundary_info.h"

void MetalSimulationRegion::DDMAC_Fill_Value(Vec x, Vec L) const
{

  // find the node variable offset
  unsigned int n_variables     = this->ebm_n_variables();
  unsigned int node_psi_offset = this->ebm_variable_offset(POTENTIAL);
  unsigned int node_Tl_offset  = this->ebm_variable_offset(TEMPERATURE);

  const PetscScalar sigma = mt->basic->Conductance();

  // data buffer
  std::vector<int> ix;
  std::vector<PetscScalar> y;
  std::vector<PetscScalar> s;

  // reserve menory for data buffer
  PetscInt n_local_dofs;
  VecGetLocalSize(x, &n_local_dofs);
  ix.reserve(n_local_dofs);
  y.reserve(n_local_dofs);
  s.reserve(n_local_dofs);

  // for all the on processor node, insert value to petsc vector
  const_processor_node_iterator node_it = on_processor_nodes_begin();
  const_processor_node_iterator node_it_end = on_processor_nodes_end();
  for(; node_it!=node_it_end; ++node_it)
  {
    const FVM_Node * fvm_node = *node_it;

    const FVM_NodeData * node_data = fvm_node->node_data();

    // psi, real part
    genius_assert(node_psi_offset!=invalid_uint);
    ix.push_back(fvm_node->global_offset() + node_psi_offset);
    y.push_back(node_data->psi());
    s.push_back(1.0/(sigma*fvm_node->volume()));

    // image part
    ix.push_back(fvm_node->global_offset() + n_variables + node_psi_offset);
    y.push_back(node_data->psi());
    s.push_back(1.0/(sigma*fvm_node->volume()));


    // for extra Tl temperature equations
    if(get_advanced_model()->enable_Tl())
    {
      // T, real part
      genius_assert(node_Tl_offset!=invalid_uint);
      ix.push_back(fvm_node->global_offset() + node_Tl_offset);
      y.push_back(node_data->T());
      s.push_back(1.0/fvm_node->volume());

      // T, image part
      ix.push_back(fvm_node->global_offset() + n_variables + node_Tl_offset);
      y.push_back(node_data->T());
      s.push_back(1.0/fvm_node->volume());
    }
  }

  if( ix.size() )
  {
    VecSetValues(x, ix.size(), &ix[0], &y[0], INSERT_VALUES) ;
    VecSetValues(L, ix.size(), &ix[0], &s[0], INSERT_VALUES) ;
  }

}





void MetalSimulationRegion::DDMAC_Fill_Matrix_Vector(Mat A, Vec b, const Mat J, const PetscScalar omega,  InsertMode &add_value_flag) const
{

  // note, we will use ADD_VALUES to set values of matrix A
  // if the previous operator is not ADD_VALUES, we should flush the matrix
  if( add_value_flag != ADD_VALUES && add_value_flag != NOT_SET_VALUES)
  {
    MatAssemblyBegin(A, MAT_FLUSH_ASSEMBLY);
    MatAssemblyEnd(A, MAT_FLUSH_ASSEMBLY);
  }

  //
  const_processor_node_iterator node_it = on_processor_nodes_begin();
  const_processor_node_iterator node_it_end = on_processor_nodes_end();
  for(; node_it!=node_it_end; ++node_it)
  {
    const FVM_Node * fvm_node = *node_it;

    // if the node on electrode boundary, continue
    if( fvm_node->boundary_id() != BoundaryInfo::invalid_id ) continue;

    this->DDMAC_Fill_Nodal_Matrix_Vector(fvm_node, A, b, J, omega, add_value_flag);
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;

}


void MetalSimulationRegion::DDMAC_Fill_Transformation_Matrix ( Mat T, const Mat J, const PetscScalar omega,  InsertMode &add_value_flag ) const
{

  // note, we will use ADD_VALUES to set values of matrix A
  // if the previous operator is not ADD_VALUES, we should flush the matrix
  if ( add_value_flag != ADD_VALUES && add_value_flag != NOT_SET_VALUES )
  {
    MatAssemblyBegin ( T, MAT_FLUSH_ASSEMBLY );
    MatAssemblyEnd ( T, MAT_FLUSH_ASSEMBLY );
  }

  // each indepedent variable
  std::vector<SolutionVariable> indepedent_variable;

  indepedent_variable.push_back ( POTENTIAL );

  if ( this->get_advanced_model()->enable_Tl() )
    indepedent_variable.push_back ( TEMPERATURE );

  //
  const_processor_node_iterator node_it = on_processor_nodes_begin();
  const_processor_node_iterator node_it_end = on_processor_nodes_end();
  for(; node_it!=node_it_end; ++node_it)
  {
    const FVM_Node * fvm_node = *node_it;

    for ( unsigned int n=0; n<indepedent_variable.size(); ++n )
    {
      PetscInt diag_index = fvm_node->global_offset() + this->ebm_variable_offset ( indepedent_variable[n] );
      PetscScalar diag_value;

      MatGetValues(J, 1, &diag_index, 1, &diag_index, &diag_value);

      PetscInt ac_real = diag_index;
      PetscInt ac_imag = diag_index + this->ebm_n_variables();
      MatSetValue ( T, ac_real, ac_real, 1.0, ADD_VALUES );
      if( indepedent_variable[n] != POTENTIAL )
      {
        MatSetValue ( T, ac_real, ac_imag, omega/diag_value, ADD_VALUES );
        MatSetValue ( T, ac_imag, ac_real, -omega/diag_value, ADD_VALUES );
      }
      MatSetValue ( T, ac_imag, ac_imag, 1.0, ADD_VALUES );
    }
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;
}





void MetalSimulationRegion::DDMAC_Fill_Nodal_Matrix_Vector(
  const FVM_Node *fvm_node, Mat A, Vec, const Mat J, const PetscScalar omega,
  InsertMode & add_value_flag,
  const SimulationRegion * adjacent_region,
  const FVM_Node * adjacent_fvm_node) const
{

  // each indepedent variable
  std::vector<SolutionVariable> indepedent_variable;

  indepedent_variable.push_back( POTENTIAL );

  if(this->get_advanced_model()->enable_Tl())
    indepedent_variable.push_back( TEMPERATURE );

  genius_assert( fvm_node->root_node()->processor_id() == Genius::processor_id() );


  const FVM_NodeData * node_data = fvm_node->node_data();
  this->mt->mapping(fvm_node->root_node(), node_data, SolverSpecify::clock);

  for(unsigned int n=0; n<indepedent_variable.size(); ++n)
  {
    PetscInt ncols;
    const PetscInt    * row_cols_pointer;
    const PetscScalar * row_vals_pointer;

    PetscInt jacobian_row = fvm_node->global_offset() + this->ebm_variable_offset(indepedent_variable[n]);

    // get derivative from Jacobian matrix
    MatGetRow(J, jacobian_row, &ncols, &row_cols_pointer, &row_vals_pointer);

    PetscInt ac_real = fvm_node->global_offset() + this->ebm_variable_offset(indepedent_variable[n]);
    PetscInt ac_imag = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(indepedent_variable[n]);
    if(adjacent_region!=NULL && adjacent_fvm_node!=NULL)
    {
      ac_real = adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(indepedent_variable[n]);
      ac_imag = adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(indepedent_variable[n]);
    }

    std::vector<PetscInt> real_row_entry, imag_row_entry;
    for(int n=0; n<ncols; ++n)
    {
      real_row_entry.push_back(row_cols_pointer[n]);
      imag_row_entry.push_back(row_cols_pointer[n]+this->ebm_n_variables());
    }

    // fill real part to AC matrix
    MatSetValues(A, 1, &ac_real, real_row_entry.size(), &real_row_entry[0], row_vals_pointer, ADD_VALUES );

    // fill image part to AC matrix
    MatSetValues(A, 1, &ac_imag, imag_row_entry.size(), &imag_row_entry[0], row_vals_pointer, ADD_VALUES );

    // restore row pointers of Jacobian matrix
    MatRestoreRow(J, jacobian_row, &ncols, &row_cols_pointer, &row_vals_pointer);
  }


  // process omega item of lattice temperature equ
  if(this->get_advanced_model()->enable_Tl())
  {
    PetscScalar HeatCapacity =  this->mt->thermal->HeatCapacity(node_data->T());

    PetscInt real_row = fvm_node->global_offset() + this->ebm_variable_offset(TEMPERATURE);
    PetscInt imag_row = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(TEMPERATURE) ;
    if(adjacent_region!=NULL && adjacent_fvm_node!=NULL)
    {
      real_row = adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(TEMPERATURE);
      imag_row = adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(TEMPERATURE) ;
    }
    MatSetValue(A, real_row, imag_row,  node_data->density()*HeatCapacity*omega*fvm_node->volume(), ADD_VALUES );
    MatSetValue(A, imag_row, real_row, -node_data->density()*HeatCapacity*omega*fvm_node->volume(), ADD_VALUES );
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;


}






void MetalSimulationRegion::DDMAC_Fill_Nodal_Matrix_Vector(
  const FVM_Node *fvm_node, const SolutionVariable var,
  Mat A, Vec , const Mat J, const PetscScalar omega, InsertMode & add_value_flag,
  const SimulationRegion * adjacent_region,
  const FVM_Node * adjacent_fvm_node) const
{

  genius_assert( fvm_node->root_node()->processor_id() == Genius::processor_id() );

  const FVM_NodeData * node_data = fvm_node->node_data();
  this->mt->mapping(fvm_node->root_node(), node_data, SolverSpecify::clock);

  PetscInt ncols;
  const PetscInt    * row_cols_pointer;
  const PetscScalar * row_vals_pointer;

  PetscInt jacobian_row = fvm_node->global_offset() + this->ebm_variable_offset(var);

  // get derivative from Jacobian matrix
  MatGetRow(J, jacobian_row, &ncols, &row_cols_pointer, &row_vals_pointer);

  PetscInt ac_real = fvm_node->global_offset() + this->ebm_variable_offset(var);
  PetscInt ac_imag = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(var);
  if(adjacent_region!=NULL && adjacent_fvm_node!=NULL)
  {
    ac_real = adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(var);
    ac_imag = adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(var);
  }

  std::vector<PetscInt> real_row_entry, imag_row_entry;
  for(int n=0; n<ncols; ++n)
  {
    real_row_entry.push_back(row_cols_pointer[n]);
    imag_row_entry.push_back(row_cols_pointer[n]+this->ebm_n_variables());
  }

  // fill real part to AC matrix
  MatSetValues(A, 1, &ac_real, real_row_entry.size(), &real_row_entry[0], row_vals_pointer, ADD_VALUES );

  // fill image part to AC matrix
  MatSetValues(A, 1, &ac_imag, imag_row_entry.size(), &imag_row_entry[0], row_vals_pointer, ADD_VALUES );

  // restore row pointers of Jacobian matrix
  MatRestoreRow(J, jacobian_row, &ncols, &row_cols_pointer, &row_vals_pointer);


  // process omega item of lattice temperature equ
  if(var == TEMPERATURE && this->get_advanced_model()->enable_Tl() )
  {
    PetscScalar HeatCapacity =  this->mt->thermal->HeatCapacity(node_data->T());

    PetscInt real_row = fvm_node->global_offset() + this->ebm_variable_offset(TEMPERATURE);
    PetscInt imag_row = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(TEMPERATURE) ;
    if(adjacent_region!=NULL && adjacent_fvm_node!=NULL)
    {
      real_row = adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(TEMPERATURE);
      imag_row = adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(TEMPERATURE) ;
    }
    MatSetValue(A, real_row, imag_row,  node_data->density()*HeatCapacity*omega*fvm_node->volume(), ADD_VALUES );
    MatSetValue(A, imag_row, real_row, -node_data->density()*HeatCapacity*omega*fvm_node->volume(), ADD_VALUES );
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;

}



void MetalSimulationRegion::DDMAC_Force_equal(const FVM_Node *fvm_node, Mat A, InsertMode & add_value_flag,
    const SimulationRegion * adjacent_region, const FVM_Node * adjacent_fvm_node) const
{
  genius_assert(add_value_flag == ADD_VALUES);

  // set entry on diag of A
  PetscInt real_row = fvm_node->global_offset() + this->ebm_variable_offset(POTENTIAL);
  PetscInt imag_row = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(POTENTIAL);
  PetscInt real_col[2]={real_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(POTENTIAL)};
  PetscInt imag_col[2]={imag_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(POTENTIAL)};
  PetscScalar diag[2] = {1.0, -1.0};
  MatSetValues(A, 1, &real_row, 2, real_col, diag, ADD_VALUES);
  MatSetValues(A, 1, &imag_row, 2, imag_col, diag, ADD_VALUES);


  if(this->get_advanced_model()->enable_Tl())
  {
    PetscInt real_row = fvm_node->global_offset() + this->ebm_variable_offset(TEMPERATURE);
    PetscInt imag_row = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(TEMPERATURE);
    PetscInt real_col[2]={real_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(TEMPERATURE)};
    PetscInt imag_col[2]={imag_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(TEMPERATURE)};
    PetscScalar diag[2] = {1.0, -1.0};
    MatSetValues(A, 1, &real_row, 2, real_col, diag, ADD_VALUES);
    MatSetValues(A, 1, &imag_row, 2, imag_col, diag, ADD_VALUES);
  }

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;
}


void MetalSimulationRegion::DDMAC_Force_equal(const FVM_Node *fvm_node, const SolutionVariable var, Mat A, InsertMode & add_value_flag,
    const SimulationRegion * adjacent_region, const FVM_Node * adjacent_fvm_node) const
{
  genius_assert(add_value_flag == ADD_VALUES);
  genius_assert(this->ebm_variable_offset(var)!=invalid_uint);
  genius_assert(adjacent_region->ebm_variable_offset(var)!=invalid_uint);

  // set entry on diag of A
  PetscInt real_row = fvm_node->global_offset() + this->ebm_variable_offset(var);
  PetscInt imag_row = fvm_node->global_offset() + this->ebm_n_variables() + this->ebm_variable_offset(var);
  PetscInt real_col[2]={real_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_variable_offset(var)};
  PetscInt imag_col[2]={imag_row, adjacent_fvm_node->global_offset() + adjacent_region->ebm_n_variables() + adjacent_region->ebm_variable_offset(var)};
  PetscScalar diag[2] = {1.0, -1.0};
  MatSetValues(A, 1, &real_row, 2, real_col, diag, ADD_VALUES);
  MatSetValues(A, 1, &imag_row, 2, imag_col, diag, ADD_VALUES);

  // the last operator is ADD_VALUES
  add_value_flag = ADD_VALUES;
}


void MetalSimulationRegion::DDMAC_Update_Solution(PetscScalar *lxx)
{
  unsigned int node_psi_offset = ebm_variable_offset(POTENTIAL);
  unsigned int node_Tl_offset  = ebm_variable_offset(TEMPERATURE);

  local_node_iterator node_it = on_local_nodes_begin();
  local_node_iterator node_it_end = on_local_nodes_end();
  for(; node_it!=node_it_end; ++node_it)
  {
    FVM_Node * fvm_node = *node_it;

    FVM_NodeData * node_data = fvm_node->node_data();  genius_assert(node_data!=NULL);

    //update psi
    {
      PetscScalar psi_real = lxx[fvm_node->local_offset() + node_psi_offset];
      PetscScalar psi_imag = lxx[fvm_node->local_offset() + this->ebm_n_variables() + node_psi_offset];
      node_data->psi_ac()  = std::complex<PetscScalar>(psi_real, psi_imag);
    }

    // lattice temperature if required
    if(this->get_advanced_model()->enable_Tl())
    {
      PetscScalar T_real = lxx[fvm_node->local_offset() + node_Tl_offset];
      PetscScalar T_imag = lxx[fvm_node->local_offset() + this->ebm_n_variables() + node_Tl_offset];
      node_data->T_ac()  = std::complex<PetscScalar>(T_real, T_imag);
    }
  }

}




