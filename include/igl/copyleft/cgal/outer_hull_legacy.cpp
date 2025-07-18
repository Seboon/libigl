// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2015 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "outer_hull_legacy.h"
#include "extract_cells.h"
#include "remesh_self_intersections.h"
#include "assign.h"
#include "../../remove_unreferenced.h"

#include <CGAL/AABB_tree.h>
#include <CGAL/AABB_traits.h>
#include <CGAL/AABB_triangle_primitive.h>
#include <CGAL/intersections.h>
#include <CGAL/Exact_predicates_exact_constructions_kernel.h>

#include "points_inside_component.h"
#include "order_facets_around_edges.h"
#include "outer_facet.h"
#include "../../sortrows.h"
#include "../../facet_components.h"
#include "../../winding_number.h"
#include "../../triangle_triangle_adjacency.h"
#include "../../unique_edge_map.h"
#include "../../barycenter.h"
#include "../../per_face_normals.h"
#include "../../PlainMatrix.h"
#include "../../sort_angles.h"
#include <Eigen/Geometry>
#include <vector>
#include <map>
#include <queue>
#include <iostream>
#include <type_traits>
#include <CGAL/number_utils.h>
//#define IGL_OUTER_HULL_DEBUG

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedG,
  typename DerivedJ,
  typename Derivedflip>
IGL_INLINE void igl::copyleft::cgal::outer_hull_legacy(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  Eigen::PlainObjectBase<DerivedG> & G,
  Eigen::PlainObjectBase<DerivedJ> & J,
  Eigen::PlainObjectBase<Derivedflip> & flip)
{
#ifdef IGL_OUTER_HULL_DEBUG
  std::cerr << "Extracting outer hull" << std::endl;
#endif
  typedef typename DerivedF::Index Index;
  Eigen::Matrix<Index,DerivedF::RowsAtCompileTime,1> C;
  typedef Eigen::Matrix<typename DerivedV::Scalar ,Eigen::Dynamic,DerivedV::ColsAtCompileTime> MatrixXV;
  //typedef Eigen::Matrix<typename DerivedF::Scalar ,Eigen::Dynamic,DerivedF::ColsAtCompileTime> MatrixXF;
  typedef Eigen::Matrix<typename DerivedG::Scalar ,Eigen::Dynamic,DerivedG::ColsAtCompileTime> MatrixXG;
  typedef Eigen::Matrix<typename DerivedJ::Scalar ,Eigen::Dynamic,DerivedJ::ColsAtCompileTime> MatrixXJ;
  const Index m = F.rows();

  // UNUSED:
  //const auto & duplicate_simplex = [&F](const int f, const int g)->bool
  //{
  //  return
  //    (F(f,0) == F(g,0) && F(f,1) == F(g,1) && F(f,2) == F(g,2)) ||
  //    (F(f,1) == F(g,0) && F(f,2) == F(g,1) && F(f,0) == F(g,2)) ||
  //    (F(f,2) == F(g,0) && F(f,0) == F(g,1) && F(f,1) == F(g,2)) ||
  //    (F(f,0) == F(g,2) && F(f,1) == F(g,1) && F(f,2) == F(g,0)) ||
  //    (F(f,1) == F(g,2) && F(f,2) == F(g,1) && F(f,0) == F(g,0)) ||
  //    (F(f,2) == F(g,2) && F(f,0) == F(g,1) && F(f,1) == F(g,0));
  //};

#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"outer hull..."<<std::endl;
#endif

#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"edge map..."<<std::endl;
#endif
  typedef Eigen::Matrix<typename DerivedF::Scalar ,Eigen::Dynamic,2> MatrixX2I;
  typedef Eigen::Matrix<typename DerivedF::Index ,Eigen::Dynamic,1> VectorXI;
  //typedef Eigen::Matrix<typename DerivedV::Scalar, 3, 1> Vector3F;
  MatrixX2I E,uE;
  VectorXI EMAP;
  std::vector<std::vector<typename DerivedF::Index> > uE2E;
  unique_edge_map(F,E,uE,EMAP,uE2E);
#ifdef IGL_OUTER_HULL_DEBUG
  for (size_t ui=0; ui<uE.rows(); ui++) {
      std::cout << ui << ": " << uE2E[ui].size() << " -- (";
      for (size_t i=0; i<uE2E[ui].size(); i++) {
          std::cout << uE2E[ui][i] << ", ";
      }
      std::cout << ")" << std::endl;
  }
#endif

  std::vector<std::vector<typename DerivedF::Index> > uE2oE;
  std::vector<std::vector<bool> > uE2C;
  order_facets_around_edges(V, F, uE, uE2E, uE2oE, uE2C);
  uE2E = uE2oE;
  VectorXI diIM(3*m);
  for (auto ue : uE2E) {
      for (size_t i=0; i<ue.size(); i++) {
          auto fe = ue[i];
          diIM[fe] = i;
      }
  }

  std::vector<std::vector<std::vector<Index > > > TT,_1;
  triangle_triangle_adjacency(E,EMAP,uE2E,false,TT,_1);
  VectorXI counts;
#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"facet components..."<<std::endl;
#endif
  facet_components(TT,C,counts);
  assert(C.maxCoeff()+1 == counts.rows());
  const size_t ncc = counts.rows();
  G.resize(0,F.cols());
  J.resize(0,1);
  flip.setConstant(m,1,false);

#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"reindex..."<<std::endl;
#endif
  // H contains list of faces on outer hull;
  std::vector<bool> FH(m,false);
  std::vector<bool> EH(3*m,false);
  std::vector<MatrixXG> vG(ncc);
  std::vector<MatrixXJ> vJ(ncc);
  std::vector<MatrixXJ> vIM(ncc);
  //size_t face_count = 0;
  for(size_t id = 0;id<ncc;id++)
  {
    vIM[id].resize(counts[id],1);
  }
  // current index into each IM
  std::vector<size_t> g(ncc,0);
  // place order of each face in its respective component
  for(Index f = 0;f<m;f++)
  {
    vIM[C(f)](g[C(f)]++) = f;
  }

#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"barycenters..."<<std::endl;
#endif
  // assumes that "resolve" has handled any coplanar cases correctly and nearly
  // coplanar cases can be sorted based on barycenter.
  MatrixXV BC;
  barycenter(V,F,BC);

#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"loop over CCs (="<<ncc<<")..."<<std::endl;
#endif
  for(Index id = 0;id<(Index)ncc;id++)
  {
    auto & IM = vIM[id];
    // starting face that's guaranteed to be on the outer hull and in this
    // component
    int f;
    bool f_flip;
#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"outer facet..."<<std::endl;
#endif
  igl::copyleft::cgal::outer_facet(V,F,IM,f,f_flip);
#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"outer facet: "<<f<<std::endl;
  //cout << V.row(F(f, 0)) << std::endl;
  //cout << V.row(F(f, 1)) << std::endl;
  //cout << V.row(F(f, 2)) << std::endl;
#endif
    int FHcount = 1;
    FH[f] = true;
    // Q contains list of face edges to continue traversing upong
    std::queue<int> Q;
    Q.push(f+0*m);
    Q.push(f+1*m);
    Q.push(f+2*m);
    flip(f) = f_flip;
    //std::cout << "face " << face_count++ << ": " << f << std::endl;
    //std::cout << "f " << F.row(f).array()+1 << std::endl;
    //cout<<"flip("<<f<<") = "<<(flip(f)?"true":"false")<<std::endl;
#ifdef IGL_OUTER_HULL_DEBUG
  std::cout<<"BFS..."<<std::endl;
#endif
    while(!Q.empty())
    {
      // face-edge
      const int e = Q.front();
      Q.pop();
      // face
      const int f = e%m;
      // corner
      const int c = e/m;
#ifdef IGL_OUTER_HULL_DEBUG
      std::cout << "edge: " << e << ", ue: " << EMAP(e) << std::endl;
      std::cout << "face: " << f << std::endl;
      std::cout << "corner: " << c << std::endl;
      std::cout << "consistent: " << uE2C[EMAP(e)][diIM[e]] << std::endl;
#endif
      // Should never see edge again...
      if(EH[e] == true)
      {
        continue;
      }
      EH[e] = true;
      // source of edge according to f
      const int fs = flip(f)?F(f,(c+2)%3):F(f,(c+1)%3);
      // destination of edge according to f
      const int fd = flip(f)?F(f,(c+1)%3):F(f,(c+2)%3);
      // edge valence
      const size_t val = uE2E[EMAP(e)].size();
#ifdef IGL_OUTER_HULL_DEBUG
      //std::cout << "vd: " << V.row(fd) << std::endl;
      //std::cout << "vs: " << V.row(fs) << std::endl;
      //std::cout << "edge: " << V.row(fd) - V.row(fs) << std::endl;
      for (size_t i=0; i<val; i++) {
          if (i == diIM(e)) {
              std::cout << "* ";
          } else {
              std::cout << "  ";
          }
          std::cout << i << ": "
              << " (e: " << uE2E[EMAP(e)][i] << ", f: "
              << uE2E[EMAP(e)][i] % m * (uE2C[EMAP(e)][i] ? 1:-1) << ")" << std::endl;
      }
#endif

      // is edge consistent with edge of face used for sorting
      const int e_cons = (uE2C[EMAP(e)][diIM(e)] ? 1: -1);
      int nfei = -1;
      // Loop once around trying to find suitable next face
      for(size_t step = 1; step<val+2;step++)
      {
        const int nfei_new = (diIM(e) + 2*val + e_cons*step*(flip(f)?-1:1))%val;
        const int nf = uE2E[EMAP(e)][nfei_new] % m;
        {
#ifdef IGL_OUTER_HULL_DEBUG
        //cout<<"Next facet: "<<(f+1)<<" --> "<<(nf+1)<<", |"<<
        //  di[EMAP(e)][diIM(e)]<<" - "<<di[EMAP(e)][nfei_new]<<"| = "<<
        //    abs(di[EMAP(e)][diIM(e)] - di[EMAP(e)][nfei_new])
        //    <<std::endl;
#endif



          // Only use this face if not already seen
          if(!FH[nf])
          {
            nfei = nfei_new;
          //} else {
          //    std::cout << "skipping face " << nfei_new << " because it is seen before"
          //        << std::endl;
          }
          break;
        //} else {
        //    std::cout << di[EMAP(e)][diIM(e)].transpose() << std::endl;
        //    std::cout << di[EMAP(e)][diIM(nfei_new)].transpose() << std::endl;
        //    std::cout << "skipping face " << nfei_new << " with identical dihedral angle"
        //        << std::endl;
        }
//#ifdef IGL_OUTER_HULL_DEBUG
//        std::cout<<"Skipping co-planar facet: "<<(f+1)<<" --> "<<(nf+1)<<std::endl;
//#endif
      }

      int max_ne = -1;
      if(nfei >= 0)
      {
        max_ne = uE2E[EMAP(e)][nfei];
      }

      if(max_ne>=0)
      {
        // face of neighbor
        const int nf = max_ne%m;
#ifdef IGL_OUTER_HULL_DEBUG
        if(!FH[nf])
        {
          // first time seeing face
          std::cout<<(f+1)<<" --> "<<(nf+1)<<std::endl;
        }
#endif
        FH[nf] = true;
        //std::cout << "face " << face_count++ << ": " << nf << std::endl;
        //std::cout << "f " << F.row(nf).array()+1 << std::endl;
        FHcount++;
        // corner of neighbor
        const int nc = max_ne/m;
        const int nd = F(nf,(nc+2)%3);
        const bool cons = (flip(f)?fd:fs) == nd;
        flip(nf) = (cons ? flip(f) : !flip(f));
        //cout<<"flip("<<nf<<") = "<<(flip(nf)?"true":"false")<<std::endl;
        const int ne1 = nf+((nc+1)%3)*m;
        const int ne2 = nf+((nc+2)%3)*m;
        if(!EH[ne1])
        {
          Q.push(ne1);
        }
        if(!EH[ne2])
        {
          Q.push(ne2);
        }
      }
    }

    {
      vG[id].resize(FHcount,3);
      vJ[id].resize(FHcount,1);
      //nG += FHcount;
      size_t h = 0;
      assert(counts(id) == IM.rows());
      for(int i = 0;i<counts(id);i++)
      {
        const size_t f = IM(i);
        //if(f_flip)
        //{
        //  flip(f) = !flip(f);
        //}
        if(FH[f])
        {
          vG[id].row(h) = (flip(f)?F.row(f).reverse().eval():F.row(f));
          vJ[id](h,0) = f;
          h++;
        }
      }
      assert((int)h == FHcount);
    }
  }

  // Is A inside B? Assuming A and B are consistently oriented but closed and
  // non-intersecting.
  const auto & has_overlapping_bbox = [](
    const Eigen::MatrixBase<DerivedV> & V,
    const MatrixXG & A,
    const MatrixXG & B)->bool
  {
    const auto & bounding_box = [](
      const Eigen::MatrixBase<DerivedV> & V,
      const MatrixXG & F)->
        PlainMatrix<DerivedV,2,3>
    {
      PlainMatrix<DerivedV,2,3> BB(2,3);
      BB<<
         1e26,1e26,1e26,
        -1e26,-1e26,-1e26;
      const size_t m = F.rows();
      for(size_t f = 0;f<m;f++)
      {
        for(size_t c = 0;c<3;c++)
        {
          const auto & vfc = V.row(F(f,c)).eval();
          BB(0,0) = std::min(BB(0,0), vfc(0,0));
          BB(0,1) = std::min(BB(0,1), vfc(0,1));
          BB(0,2) = std::min(BB(0,2), vfc(0,2));
          BB(1,0) = std::max(BB(1,0), vfc(0,0));
          BB(1,1) = std::max(BB(1,1), vfc(0,1));
          BB(1,2) = std::max(BB(1,2), vfc(0,2));
        }
      }
      return BB;
    };
    // A lot of the time we're dealing with unrelated, distant components: cull
    // them.
    PlainMatrix<DerivedV,2,3> ABB = bounding_box(V,A);
    PlainMatrix<DerivedV,2,3> BBB = bounding_box(V,B);
    if( (BBB.row(0)-ABB.row(1)).maxCoeff()>0  ||
        (ABB.row(0)-BBB.row(1)).maxCoeff()>0 )
    {
      // bounding boxes do not overlap
      return false;
    } else {
      return true;
    }
  };

  // Reject components which are completely inside other components
  std::vector<bool> keep(ncc,true);
  size_t nG = 0;
  // This is O( ncc * ncc * m)
  for(size_t id = 0;id<ncc;id++)
  {
    if (!keep[id]) continue;
    std::vector<size_t> unresolved;
    for(size_t oid = 0;oid<ncc;oid++)
    {
      if(id == oid || !keep[oid])
      {
        continue;
      }
      if (has_overlapping_bbox(V, vG[id], vG[oid])) {
          unresolved.push_back(oid);
      }
    }
    const size_t num_unresolved_components = unresolved.size();
    PlainMatrix<DerivedV,Eigen::Dynamic,3> query_points(num_unresolved_components, 3);
    for (size_t i=0; i<num_unresolved_components; i++) {
        const size_t oid = unresolved[i];
        PlainMatrix<DerivedF,1> f = vG[oid].row(0);
        query_points(i,0) = (V(f(0,0), 0) + V(f(0,1), 0) + V(f(0,2), 0))/3.0;
        query_points(i,1) = (V(f(0,0), 1) + V(f(0,1), 1) + V(f(0,2), 1))/3.0;
        query_points(i,2) = (V(f(0,0), 2) + V(f(0,1), 2) + V(f(0,2), 2))/3.0;
    }
    Eigen::VectorXi inside;
    igl::copyleft::cgal::points_inside_component(V, vG[id], query_points, inside);
    assert((size_t)inside.size() == num_unresolved_components);
    for (size_t i=0; i<num_unresolved_components; i++) {
        if (inside(i, 0)) {
            const size_t oid = unresolved[i];
            keep[oid] = false;
        }
    }
  }
  for (size_t id = 0; id<ncc; id++) {
      if (keep[id]) {
          nG += vJ[id].rows();
      }
  }

  // collect G and J across components
  G.resize(nG,3);
  J.resize(nG,1);
  {
    size_t off = 0;
    for(Index id = 0;id<(Index)ncc;id++)
    {
      if(keep[id])
      {
        assert(vG[id].rows() == vJ[id].rows());
        G.block(off,0,vG[id].rows(),vG[id].cols()) = vG[id];
        J.block(off,0,vJ[id].rows(),vJ[id].cols()) = vJ[id];
        off += vG[id].rows();
      }
    }
  }
}


#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
// generated by autoexplicit.sh
template void igl::copyleft::cgal::outer_hull_legacy<Eigen::Matrix<CGAL::Epeck::FT, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<CGAL::Epeck::FT, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> >&);
template void igl::copyleft::cgal::outer_hull_legacy< Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, 1, 0, -1, 1>, Eigen::Matrix<int, -1, 1, 0, -1, 1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > &, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > &, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, 1, 0, -1, 1> > &);
template void igl::copyleft::cgal::outer_hull_legacy<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#ifdef WIN32
#endif
#endif
