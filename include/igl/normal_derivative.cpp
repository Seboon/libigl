// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "LinSpaced.h"
#include "normal_derivative.h"
#include "cotmatrix_entries.h"
#include "placeholders.h"
#include <cassert>

template <
  typename DerivedV,
  typename DerivedEle,
  typename Scalar>
IGL_INLINE void igl::normal_derivative(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedEle> & Ele,
  Eigen::SparseMatrix<Scalar>& DD)
{
  // Element simplex-size
  const size_t ss = Ele.cols();
  assert( ((ss==3) || (ss==4)) && "Only triangles or tets");
  // cotangents
  Eigen::Matrix<Scalar ,Eigen::Dynamic ,Eigen::Dynamic> C;
  cotmatrix_entries(V,Ele,C);
  std::vector<Eigen::Triplet<Scalar> > IJV;
  // Number of elements
  const size_t m = Ele.rows();
  // Number of vertices
  const size_t n = V.rows();
  switch(ss)
  {
    default:
      assert(false);
      return;
    case 4:
    {
      const Eigen::MatrixXi DDJ =
        Ele(igl::placeholders::all,{1,0,2,0,3,0,2,1,3,1,0,1,3,2,0,2,1,2,0,3,1,3,2,3});
      Eigen::MatrixXi DDI(m,24);
      for(size_t f = 0;f<4;f++)
      {
        const auto & I = (igl::LinSpaced<Eigen::VectorXi >(m,0,m-1).array()+f*m).eval();
        for(size_t r = 0;r<6;r++)
        {
          DDI.col(f*6+r) = I;
        }
      }
      const Eigen::DiagonalMatrix<Scalar,24,24> S =
        (Eigen::Matrix<Scalar,2,1>(1,-1).template replicate<12,1>()).asDiagonal();
      Eigen::Matrix<Scalar ,Eigen::Dynamic ,Eigen::Dynamic> DDV =
        C(igl::placeholders::all,{2,2,1,1,3,3,0,0,4,4,2,2,5,5,1,1,0,0,3,3,4,4,5,5});
      DDV *= S;

      IJV.reserve(DDV.size());
      for(size_t f = 0;f<6*4;f++)
      {
        for(size_t e = 0;e<m;e++)
        {
          IJV.push_back(Eigen::Triplet<Scalar>(DDI(e,f),DDJ(e,f),DDV(e,f)));
        }
      }
      DD.resize(m*4,n);
      DD.setFromTriplets(IJV.begin(),IJV.end());
      break;
    }
    case 3:
    {
      const Eigen::MatrixXi DDJ = Ele(igl::placeholders::all,{2,0,1,0,0,1,2,1,1,2,0,2});
      Eigen::MatrixXi DDI(m,12);
      for(size_t f = 0;f<3;f++)
      {
        const auto & I = (igl::LinSpaced<Eigen::VectorXi >(m,0,m-1).array()+f*m).eval();
        for(size_t r = 0;r<4;r++)
        {
          DDI.col(f*4+r) = I;
        }
      }
      const Eigen::DiagonalMatrix<Scalar,12,12> S =
        (Eigen::Matrix<Scalar,2,1>(1,-1).template replicate<6,1>()).asDiagonal();
      Eigen::Matrix<Scalar ,Eigen::Dynamic ,Eigen::Dynamic> DDV = C(igl::placeholders::all,{1,1,2,2,2,2,0,0,0,0,1,1});
      DDV *= S;

      IJV.reserve(DDV.size());
      for(size_t f = 0;f<12;f++)
      {
        for(size_t e = 0;e<m;e++)
        {
          IJV.push_back(Eigen::Triplet<Scalar>(DDI(e,f),DDJ(e,f),DDV(e,f)));
        }
      }
      DD.resize(m*3,n);
      DD.setFromTriplets(IJV.begin(),IJV.end());
      break;
    }
  }

}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
template void igl::normal_derivative<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, double>(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::SparseMatrix<double, 0, int>&);
#endif
