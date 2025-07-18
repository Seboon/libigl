// This file is part of libigl, a simple c++ geometry processing library.
// 
// Copyright (C) 2014 Alec Jacobson <alecjacobson@gmail.com>
// 
// This Source Code Form is subject to the terms of the Mozilla Public License 
// v. 2.0. If a copy of the MPL was not distributed with this file, You can 
// obtain one at http://mozilla.org/MPL/2.0/.

// # MUTUAL DEPENDENCY ISSUE FOR HEADER ONLY VERSION
// MUST INCLUDE winding_number.h first before guard:
#include "winding_number.h"

#ifndef IGL_WINDINGNUMBERAABB_H
#define IGL_WINDINGNUMBERAABB_H
#include "WindingNumberTree.h"
#include "PlainMatrix.h"

namespace igl
{
  /// Class for building an AABB tree to implement the divide and conquer
  /// algorithm described in [Jacobson et al. 2013]. 
  template <
    typename Scalar, 
    typename Index>
  class WindingNumberAABB : public WindingNumberTree<Scalar,Index>
  {
    protected:
      // WindingNumberTree defines Point
      using Point = typename WindingNumberTree<Scalar,Index>::Point;
      using MatrixXF = typename WindingNumberTree<Scalar,Index>::MatrixXF;
      Point min_corner;
      Point max_corner;
      Scalar total_positive_area;
    public: 
      enum SplitMethod
      {
        CENTER_ON_LONGEST_AXIS = 0,
        MEDIAN_ON_LONGEST_AXIS = 1,
        NUM_SPLIT_METHODS = 2
      } split_method;
    public:
      inline WindingNumberAABB():
        total_positive_area(std::numeric_limits<Scalar>::infinity()),
        split_method(MEDIAN_ON_LONGEST_AXIS)
      {}
      /// Constructor
      ///
      /// @param[in] V  #V by 3 list of vertex positions
      /// @param[in] F  #F by 3 list of triangle indices into V
      template <typename DerivedV, typename DerivedF>
      inline WindingNumberAABB(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);
      inline WindingNumberAABB(
        const WindingNumberTree<Scalar,Index> & parent,
        const typename WindingNumberTree<Scalar,Index>::MatrixXF & F);
      /// Initialize the hierarchy to a given mesh
      ///
      /// @param[in] V  #V by 3 list of vertex positions
      /// @param[in] F  #F by 3 list of triangle indices into V
      template <typename DerivedV, typename DerivedF>
      inline void set_mesh(
        const Eigen::MatrixBase<DerivedV> & V,
        const Eigen::MatrixBase<DerivedF> & F);
      inline void init();
      inline bool inside(const Point & p) const;
      inline virtual void grow();
      // Compute min and max corners
      inline void compute_min_max_corners();
      inline Scalar max_abs_winding_number(const Point & p) const;
      inline Scalar max_simple_abs_winding_number(const Point & p) const;
  };
}

// Implementation

#include "winding_number.h"

#include "barycenter.h"
#include "median.h"
#include "doublearea.h"
#include "per_face_normals.h"

#include <limits>
#include <vector>
#include <iostream>

// Minimum number of faces in a hierarchy element (this is probably dependent
// on speed of machine and compiler optimization)
#ifndef WindingNumberAABB_MIN_F
#  define WindingNumberAABB_MIN_F 100
#endif

template <typename Scalar, typename Index>
  template <typename DerivedV, typename DerivedF>
inline void igl::WindingNumberAABB<Scalar,Index>::set_mesh(
    const Eigen::MatrixBase<DerivedV> & V,
    const Eigen::MatrixBase<DerivedF> & F)
{
  // static assert that DerivedF::ColsAtCompileTime == 3 or Eigen::Dynamic
  static_assert(
    DerivedF::ColsAtCompileTime == 3 || DerivedF::ColsAtCompileTime == Eigen::Dynamic,
    "F should have 3 or Dynamic columns");
  igl::WindingNumberTree<Scalar,Index>::set_mesh(V,F);
  init();
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::init()
{
  assert(max_corner.size() == 3);
  assert(min_corner.size() == 3);
  compute_min_max_corners();
  Eigen::Matrix<Scalar,Eigen::Dynamic,1> dblA;
  doublearea((*this->Vptr),(this->F),dblA);
  total_positive_area = dblA.sum()/2.0;
}

template <typename Scalar, typename Index>
template <typename DerivedV, typename DerivedF>
inline igl::WindingNumberAABB<Scalar,Index>::WindingNumberAABB(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F):
  WindingNumberTree<Scalar,Index>(V,F),
  min_corner(),
  max_corner(),
  total_positive_area(
    std::numeric_limits<Scalar>::infinity()),
  split_method(MEDIAN_ON_LONGEST_AXIS)
{
  init();
}

template <typename Scalar, typename Index>
inline igl::WindingNumberAABB<Scalar,Index>::WindingNumberAABB(
  const WindingNumberTree<Scalar,Index> & parent,
  const typename WindingNumberTree<Scalar,Index>::MatrixXF & F):
  WindingNumberTree<Scalar,Index>(parent,F),
  min_corner(),
  max_corner(),
  total_positive_area(
    std::numeric_limits<Scalar>::infinity()),
  split_method(MEDIAN_ON_LONGEST_AXIS)
{
  init();
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::grow()
{
  // Clear anything that already exists
  this->delete_children();

  //cout<<"cap.rows(): "<<(this->cap).rows()<<endl;
  //cout<<"F.rows(): "<<(this->F).rows()<<endl;

  // Base cases
  if(
    (this->F).rows() <= (WindingNumberAABB_MIN_F>0?WindingNumberAABB_MIN_F:0) ||
    ((this->cap).rows() - 2) >= (this->F).rows())
  {
    // Don't grow
    return;
  }

  // Compute longest direction
  int max_d = -1;
  Scalar max_len = 
    -std::numeric_limits<Scalar>::infinity();
  for(int d = 0;d<min_corner.size();d++)
  {
    if( (max_corner[d] - min_corner[d]) > max_len )
    {
      max_len = (max_corner[d] - min_corner[d]);
      max_d = d;
    }
  }
  // Compute facet barycenters
  Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic> BC;
  barycenter((*this->Vptr),(this->F),BC);


  // Blerg, why is selecting rows so difficult

  Scalar split_value;
  // Split in longest direction
  switch(split_method)
  {
    case MEDIAN_ON_LONGEST_AXIS:
      // Determine median
      median(BC.col(max_d),split_value);
      break;
    default:
      assert(false);
    case CENTER_ON_LONGEST_AXIS:
      split_value = 0.5*(max_corner[max_d] + min_corner[max_d]);
      break;
  }
  //cout<<"c: "<<0.5*(max_corner[max_d] + min_corner[max_d])<<" "<<
  //  "m: "<<split_value<<endl;;

  std::vector<int> id( (this->F).rows());
  for(int i = 0;i<(this->F).rows();i++)
  {
    if(BC(i,max_d) <= split_value)
    {
      id[i] = 0; //left
    }else
    {
      id[i] = 1; //right
    }
  }

  const int lefts = (int) count(id.begin(),id.end(),0);
  const int rights = (int) count(id.begin(),id.end(),1);
  if(lefts == 0 || rights == 0)
  {
    // badly balanced base case (could try to recut)
    return;
  }
  assert(lefts+rights == (this->F).rows());
  MatrixXF leftF(lefts,  (this->F).cols());
  MatrixXF rightF(rights,(this->F).cols());
  int left_i = 0;
  int right_i = 0;
  for(int i = 0;i<(this->F).rows();i++)
  {
    if(id[i] == 0)
    {
      leftF.row(left_i++) = (this->F).row(i);
    }else if(id[i] == 1)
    {
      rightF.row(right_i++) = (this->F).row(i);
    }else
    {
      assert(false);
    }
  }
  assert(right_i == rightF.rows());
  assert(left_i == leftF.rows());
  // Finally actually grow children and Recursively grow
  WindingNumberAABB<Scalar,Index> * leftWindingNumberAABB = 
    new WindingNumberAABB<Scalar,Index>(*this,leftF);
  leftWindingNumberAABB->grow();
  this->children.push_back(leftWindingNumberAABB);
  WindingNumberAABB<Scalar,Index> * rightWindingNumberAABB = 
    new WindingNumberAABB<Scalar,Index>(*this,rightF);
  rightWindingNumberAABB->grow();
  this->children.push_back(rightWindingNumberAABB);
}

template <typename Scalar, typename Index>
inline bool igl::WindingNumberAABB<Scalar,Index>::inside(const Point & p) const
{
  assert(p.size() == max_corner.size());
  assert(p.size() == min_corner.size());
  for(int i = 0;i<p.size();i++)
  {
    //// Perfect matching is **not** robust
    //if( p(i) < min_corner(i) || p(i) >= max_corner(i))
    // **MUST** be conservative
    if( p(i) < min_corner(i) || p(i) > max_corner(i))
    {
      return false;
    }
  }
  return true;
}

template <typename Scalar, typename Index>
inline void igl::WindingNumberAABB<Scalar,Index>::compute_min_max_corners()
{
  // initialize corners
  for(int d = 0;d<min_corner.size();d++)
  {
    min_corner[d] =  std::numeric_limits<typename Point::Scalar>::infinity();
    max_corner[d] = -std::numeric_limits<typename Point::Scalar>::infinity();
  }

  this->center = Point(0,0,0);
  // Loop over facets
  for(int i = 0;i<(this->F).rows();i++)
  {
    for(int j = 0;j<(this->F).cols();j++)
    {
      for(int d = 0;d<min_corner.size();d++)
      {
        min_corner[d] = 
          (*this->Vptr)((this->F)(i,j),d) < min_corner[d] ?  
            (*this->Vptr)((this->F)(i,j),d) : min_corner[d];
        max_corner[d] = 
          (*this->Vptr)((this->F)(i,j),d) > max_corner[d] ?  
            (*this->Vptr)((this->F)(i,j),d) : max_corner[d];
      }
      // This is biased toward vertices incident on more than one face, but
      // perhaps that's good
      this->center += (*this->Vptr).row((this->F)(i,j));
    }
  }
  // Average
  this->center.array() /= (this->F).size();

  //cout<<"min_corner: "<<this->min_corner.transpose()<<endl;
  //cout<<"Center: "<<this->center.transpose()<<endl;
  //cout<<"max_corner: "<<this->max_corner.transpose()<<endl;
  //cout<<"Diag center: "<<((this->max_corner + this->min_corner)*0.5).transpose()<<endl;
  //cout<<endl;

  this->radius = (max_corner-min_corner).norm()/2.0;
}

template <typename Scalar, typename Index>
inline Scalar
igl::WindingNumberAABB<Scalar,Index>::max_abs_winding_number(const Point & p) const
{
  // Only valid if not inside
  if(inside(p))
  {
    return std::numeric_limits<Scalar>::infinity();
  }
  // Q: we know the total positive area so what's the most this could project
  // to? Remember it could be layered in the same direction.
  return std::numeric_limits<Scalar>::infinity();
}

template <typename Scalar, typename Index>
inline Scalar 
  igl::WindingNumberAABB<Scalar,Index>::max_simple_abs_winding_number(
  const Point & p) const
{
  // Only valid if not inside
  if(inside(p))
  {
    return std::numeric_limits<Scalar>::infinity();
  }
  // Max simple is the same as sum of positive winding number contributions of
  // bounding box

  // begin precomputation
  //MatrixXd BV((int)pow(2,3),3);
  typedef
    Eigen::Matrix<Scalar,Eigen::Dynamic,Eigen::Dynamic>
    MatrixXS;
  typedef
    Eigen::Matrix<Index,Eigen::Dynamic,Eigen::Dynamic>
    MatrixXF;
  MatrixXS BV((int)(1<<3),3);
  BV <<
    min_corner[0],min_corner[1],min_corner[2],
    min_corner[0],min_corner[1],max_corner[2],
    min_corner[0],max_corner[1],min_corner[2],
    min_corner[0],max_corner[1],max_corner[2],
    max_corner[0],min_corner[1],min_corner[2],
    max_corner[0],min_corner[1],max_corner[2],
    max_corner[0],max_corner[1],min_corner[2],
    max_corner[0],max_corner[1],max_corner[2];
  MatrixXF BF(2*2*3,3);
  BF <<
    0,6,4,
    0,2,6,
    0,3,2,
    0,1,3,
    2,7,6,
    2,3,7,
    4,6,7,
    4,7,5,
    0,4,5,
    0,5,1,
    1,5,7,
    1,7,3;
  MatrixXS BFN;
  per_face_normals(BV,BF,BFN);
  // end of precomputation

  // Only keep those with positive dot products
  MatrixXF PBF(BF.rows(),BF.cols());
  int pbfi = 0;
  Point p2c = 0.5*(min_corner+max_corner)-p;
  for(int i = 0;i<BFN.rows();i++)
  {
    if(p2c.dot(BFN.row(i)) > 0)
    {
      PBF.row(pbfi++) = BF.row(i);
    }
  }
  PBF.conservativeResize(pbfi,PBF.cols());
  return igl::winding_number(BV,PBF,p);
}

#endif
