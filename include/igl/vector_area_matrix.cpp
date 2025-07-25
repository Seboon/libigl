// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2014 Daniele Panozzo <daniele.panozzo@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "vector_area_matrix.h"
#include "boundary_facets.h"
#include <vector>

// Bug in unsupported/Eigen/SparseExtra needs iostream first
#include <iostream>
#include <unsupported/Eigen/SparseExtra>

template <typename DerivedF, typename Scalar>
IGL_INLINE void igl::vector_area_matrix(
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::SparseMatrix<Scalar>& A)
{
  // number of vertices
  const int n = F.maxCoeff()+1;

  assert(F.cols() == 3);
  Eigen::MatrixXi E;
  boundary_facets(F,E);

  //Prepare a vector of triplets to set the matrix
  std::vector<Eigen::Triplet<Scalar> > tripletList;
  tripletList.reserve(4*E.rows());

  for(int k = 0; k < E.rows(); k++)
  {
		int i = E(k,0);
		int j = E(k,1);
        tripletList.push_back(Eigen::Triplet<Scalar>(i+n, j, -0.25));
        tripletList.push_back(Eigen::Triplet<Scalar>(j, i+n, -0.25));
        tripletList.push_back(Eigen::Triplet<Scalar>(i, j+n, 0.25));
        tripletList.push_back(Eigen::Triplet<Scalar>(j+n, i, 0.25));
  }

  //Set A from triplets (Eigen will sum triplets with same coordinates)
  A.resize(n * 2, n * 2);
  A.setFromTriplets(tripletList.begin(), tripletList.end());
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::vector_area_matrix<Eigen::Matrix<int, -1, -1, 0, -1, -1>, double>(Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::SparseMatrix<double, 0, int>&);
#endif
