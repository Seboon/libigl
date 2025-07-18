// This file is part of libigl, a simple c++ geometry processing library.
//
// Copyright (C) 2017 Alec Jacobson <alecjacobson@gmail.com>
//
// This Source Code Form is subject to the terms of the Mozilla Public License
// v. 2.0. If a copy of the MPL was not distributed with this file, You can
// obtain one at http://mozilla.org/MPL/2.0/.
#include "straighten_seams.h"
#include "LinSpaced.h"
#include "on_boundary.h"
#include "sparse.h"
#include "max.h"
#include "count.h"
#include "any.h"
#include "slice.h"
#include "slice_mask.h"
#include "unique_simplices.h"
#include "adjacency_matrix.h"
#include "setxor.h"
#include "edges_to_path.h"
#include "ramer_douglas_peucker.h"
#include "vertex_components.h"
#include "list_to_matrix.h"
#include "placeholders.h"
#include "ears.h"
#include "sum.h"
#include "find.h"
#include <iostream>

template <
  typename DerivedV,
  typename DerivedF,
  typename DerivedVT,
  typename DerivedFT,
  typename Scalar,
  typename DerivedUE,
  typename DerivedUT,
  typename DerivedOT>
IGL_INLINE void igl::straighten_seams(
  const Eigen::MatrixBase<DerivedV> & V,
  const Eigen::MatrixBase<DerivedF> & F,
  const Eigen::MatrixBase<DerivedVT> & VT,
  const Eigen::MatrixBase<DerivedFT> & FT,
  const Scalar tol,
  Eigen::PlainObjectBase<DerivedUE> & UE,
  Eigen::PlainObjectBase<DerivedUT> & UT,
  Eigen::PlainObjectBase<DerivedOT> & OT)
{
  // number of faces
  assert(FT.rows() == F.rows() && "#FT must == #F");
  assert(F.cols() == 3 && "F should contain triangles");
  assert(FT.cols() == 3 && "FT should contain triangles");
  const int m = F.rows();
  // Boundary edges of the texture map and 3d meshes
  Eigen::Array<bool ,Eigen::Dynamic,1> _;
  Eigen::Array<bool ,Eigen::Dynamic,3> BT,BF;
  on_boundary(FT,_,BT);
  on_boundary(F,_,BF);
  assert((!((BF && (BT!=true)).any())) && 
    "Not dealing with boundaries of mesh that get 'stitched' in texture mesh");
  typedef Eigen::Matrix<typename DerivedF::Scalar ,Eigen::Dynamic,2> MatrixX2I;
  const MatrixX2I ET = (MatrixX2I(FT.rows()*3,2)
    <<FT.col(1),FT.col(2),FT.col(2),FT.col(0),FT.col(0),FT.col(1)).finished();
  // "half"-edges with indices into 3D-mesh
  const MatrixX2I EF = (MatrixX2I(F.rows()*3,2)
    <<F.col(1),F.col(2),F.col(2),F.col(0),F.col(0),F.col(1)).finished();
  // Find unique (undirected) edges in F
  Eigen::VectorXi EFMAP;
  {
    MatrixX2I _1;
    Eigen::VectorXi _2;
    unique_simplices(EF,_1,_2,EFMAP);
  }
  Eigen::Array<bool ,Eigen::Dynamic,1>vBT = Eigen::Map<Eigen::Array<bool ,Eigen::Dynamic,1> >(BT.data(),BT.size(),1);
  Eigen::Array<bool ,Eigen::Dynamic,1>vBF = Eigen::Map<Eigen::Array<bool ,Eigen::Dynamic,1> >(BF.data(),BF.size(),1);
  const auto vBT_i = igl::find(vBT);
  MatrixX2I OF = EF(vBT_i,igl::placeholders::all);
  OT = EF(vBT_i,igl::placeholders::all);
  Eigen::VectorXi OFMAP = EFMAP(vBT_i);
  // Two boundary edges on the texture-mapping are "equivalent" to each other on
  // the 3D-mesh if their 3D-mesh vertex indices match
  Eigen::SparseMatrix<bool> OEQ;
  {
    Eigen::SparseMatrix<bool> OEQR;
    sparse(
      igl::LinSpaced<Eigen::VectorXi >(OT.rows(),0,OT.rows()-1),
      OFMAP,
      Eigen::Array<bool ,Eigen::Dynamic,1>::Ones(OT.rows(),1),
      OT.rows(),
      m*3,
      OEQR);
    OEQ = OEQR * OEQR.transpose();
    // Remove diagonal
    OEQ.prune([](const int r, const int c, const bool)->bool{return r!=c;});
  }
  // For each edge in OT, for each endpoint, how many _other_ texture-vertices
  // are images of all the 3d-mesh vertices in F who map from "corners" in F/FT
  // mapping to this endpoint.
  //
  // Adjacency matrix between 3d-vertices and texture-vertices
  Eigen::SparseMatrix<bool> V2VT;
  sparse(
    F,
    FT,
    Eigen::Array<bool ,Eigen::Dynamic,3>::Ones(F.rows(),F.cols()),
    V.rows(),
    VT.rows(),
    V2VT);
  // For each 3d-vertex count how many different texture-coordinates its getting
  // from different incident corners
  Eigen::VectorXi DV;
  count(V2VT,2,DV);
  Eigen::VectorXi M,I;
  max(V2VT,1,M,I);
  assert( (M.array() == 1).all() );
  Eigen::VectorXi DT;
  // Map counts onto texture-vertices
  DT = DV(I,igl::placeholders::all);
  // Boundary in 3D && UV
  Eigen::Array<bool ,Eigen::Dynamic,1> BTF = vBF(igl::find(vBT));

  // Texture-vertex is "sharp" if incident on "half-"edge that is not a
  // boundary in the 3D mesh but is a boundary in the texture-mesh AND is not
  // "cut cleanly" (the vertex is mapped to exactly 2 locations)
  Eigen::Array<bool ,Eigen::Dynamic,1> SV = Eigen::Array<bool ,Eigen::Dynamic,1>::Zero(VT.rows(),1);
  //std::cout<<"#SV: "<<SV.count()<<std::endl;
  assert(BTF.size() == OT.rows());
  for(int h = 0;h<BTF.size();h++)
  {
    if(!BTF(h))
    {
      SV(OT(h,0)) = true;
      SV(OT(h,1)) = true;
    }
  }
  //std::cout<<"#SV: "<<SV.count()<<std::endl;
  Eigen::Array<bool ,Eigen::Dynamic,1> CL = DT.array()==2;
  Eigen::SparseMatrix<bool> VTOT;
  {
    Eigen::MatrixXi I = 
      igl::LinSpaced<Eigen::VectorXi >(OT.rows(),0,OT.rows()-1).replicate(1,2);
    sparse(
      OT,
      I,
      Eigen::Array<bool ,Eigen::Dynamic,2>::Ones(OT.rows(),OT.cols()),
      VT.rows(),
      OT.rows(),
      VTOT);
    Eigen::Array<int ,Eigen::Dynamic,1> cuts;
    count( (VTOT*OEQ).eval(), 2, cuts);
    CL = (CL && (cuts.array() == 2)).eval();
  }
  //std::cout<<"#CL: "<<CL.count()<<std::endl;
  assert(CL.size() == SV.size());
  for(int c = 0;c<CL.size();c++) if(CL(c)) SV(c) = false;
  {}
  //std::cout<<"#SV: "<<SV.count()<<std::endl;

  {
    // vertices at the corner of ears are declared to be sharp. This is
    // conservative: for example, if the ear is strictly convex and stays
    // strictly convex then the ear won't be flipped.
    Eigen::VectorXi ear,ear_opp;
    ears(FT,ear,ear_opp);
    //std::cout<<"#ear: "<<ear.size()<<std::endl;
    // There might be an ear on one copy, so mark vertices on other copies, too
    // ears as they live on the 3D mesh
    Eigen::Array<bool ,Eigen::Dynamic,1> earT = Eigen::Array<bool ,Eigen::Dynamic,1>::Zero(VT.rows(),1);
    for(int e = 0;e<ear.size();e++) earT(FT(ear(e),ear_opp(e))) = 1;
    //std::cout<<"#earT: "<<earT.count()<<std::endl;
    // Even if ear-vertices are marked as sharp if it changes, e.g., from
    // convex to concave then it will _force_ a flip of the ear triangle. So,
    // declare that neighbors of ears are also sharp.
    Eigen::SparseMatrix<bool> A;
    adjacency_matrix(FT,A);
    earT = (earT || (A*earT.matrix()).array()).eval();
    //std::cout<<"#earT: "<<earT.count()<<std::endl;
    assert(earT.size() == SV.size());
    for(int e = 0;e<earT.size();e++) if(earT(e)) SV(e) = true;
    //std::cout<<"#SV: "<<SV.count()<<std::endl;
  }

  {
    Eigen::SparseMatrix<bool> V2VTSV,V2VTC;
    slice_mask(V2VT,SV,2,V2VTSV);
    Eigen::Array<bool ,Eigen::Dynamic,1> Cb;
    any(V2VTSV,2,Cb);
    slice_mask(V2VT,Cb,1,V2VTC);
    any(V2VTC,1,SV);
  }
  //std::cout<<"#SV: "<<SV.count()<<std::endl;

  Eigen::SparseMatrix<bool> OTVT = VTOT.transpose();
  int nc;
  Eigen::ArrayXi C;
  {
    // Doesn't Compile on older Eigen:
    //SparseMatrix<bool> A = OTVT * (!SV).matrix().asDiagonal() * VTOT;
    Eigen::SparseMatrix<bool> A = OTVT * (SV!=true).matrix().asDiagonal() * VTOT;
    vertex_components(A,C);
    nc = C.maxCoeff()+1;
  }
  //std::cout<<"nc: "<<nc<<std::endl;
  // New texture-vertex locations
  UT = VT;
  // Indices into UT of coarse output polygon edges
  std::vector<std::vector<typename DerivedUE::Scalar> > vUE;
  // loop over each component
  std::vector<bool> done(nc,false);
  for(int c = 0;c<nc;c++)
  {
    if(done[c])
    {
      continue;
    }
    done[c] = true;
    // edges of this component
    Eigen::VectorXi Ic;
    find(C==c,Ic);
    if(Ic.size() == 0)
    {
      continue;
    }
    Eigen::SparseMatrix<bool> OEQIc;
    slice(OEQ,Ic,1,OEQIc);
    Eigen::VectorXi N;
    sum(OEQIc,2,N);
    const int ncopies = N(0)+1;
    assert((N.array() == ncopies-1).all());
    assert((ncopies == 1 || ncopies == 2) && 
      "Not dealing with non-manifold meshes");
    Eigen::VectorXi vpath,epath,eend;
    typedef Eigen::Matrix<Scalar,Eigen::Dynamic,2> MatrixX2S;
    switch(ncopies)
    {
      case 1:
        {
          MatrixX2I OTIc = OT(Ic,igl::placeholders::all);
          edges_to_path(OTIc,vpath,epath,eend);
          Eigen::Array<bool ,Eigen::Dynamic,1> SVvpath = SV(vpath);
          assert(
            (vpath(0) != vpath(vpath.size()-1) || !SVvpath.any()) && 
            "Not dealing with 1-loops touching 'sharp' corners");
          // simple open boundary
          MatrixX2S PI = VT(vpath,igl::placeholders::all);
          const Scalar bbd = 
            (PI.colwise().maxCoeff() - PI.colwise().minCoeff()).norm();
          // Do not collapse boundaries to fewer than 3 vertices
          const bool allow_boundary_collapse = false;
          assert(PI.size() >= 2);
          const bool is_closed = PI(0) == PI(PI.size()-1);
          assert(!is_closed ||  vpath.size() >= 4);
          Scalar eff_tol = std::min(tol,2.);
          Eigen::VectorXi UIc;
          while(true)
          {
            MatrixX2S UPI,UTvpath;
            ramer_douglas_peucker(PI,eff_tol*bbd,UPI,UIc,UTvpath);
            UT(vpath,igl::placeholders::all) = UTvpath;
            if(!is_closed || allow_boundary_collapse)
            {
              break;
            }
            if(UPI.rows()>=4)
            {
              break;
            }
            eff_tol = eff_tol*0.5;
          }
          for(int i = 0;i<UIc.size()-1;i++)
          {
            vUE.push_back({vpath(UIc(i)),vpath(UIc(i+1))});
          }
        }
        break;
      case 2:
        {
          // Find copies
          Eigen::VectorXi Icc;
          {
            Eigen::VectorXi II;
            Eigen::Array<bool ,Eigen::Dynamic,1> IV;
            Eigen::SparseMatrix<bool> OEQIcT = OEQIc.transpose().eval();
            find(OEQIcT,Icc,II,IV);
            assert(II.size() == Ic.size() && 
              (II.array() ==
              igl::LinSpaced<Eigen::VectorXi >(Ic.size(),0,Ic.size()-1).array()).all());
            assert(Icc.size() == Ic.size());
            const int cc = C(Icc(0));
            Eigen::VectorXi CIcc = C(Icc);
            assert((CIcc.array() == cc).all());
            assert(!done[cc]);
            done[cc] = true;
          }
          Eigen::Array<bool ,Eigen::Dynamic,1> flipped;
          {
            MatrixX2I OFIc = OF(Ic,igl::placeholders::all);
            MatrixX2I OFIcc = OF(Icc,igl::placeholders::all);
            Eigen::VectorXi XOR,IA,IB;
            setxor(OFIc,OFIcc,XOR,IA,IB);
            assert(XOR.size() == 0);
            flipped = OFIc.array().col(0) != OFIcc.array().col(0);
          }
          if(Ic.size() == 1)
          {
            // No change to UT
            vUE.push_back({OT(Ic(0),0),OT(Ic(0),1)});
            assert(Icc.size() == 1);
            vUE.push_back({OT(Icc(0),flipped(0)?1:0),OT(Icc(0),flipped(0)?0:1)});
          }else
          {
            MatrixX2I OTIc = OT(Ic,igl::placeholders::all);
            edges_to_path(OTIc,vpath,epath,eend);
            // Flip endpoints if needed
            for(int e = 0;e<eend.size();e++)if(flipped(e))eend(e)=1-eend(e);
            Eigen::VectorXi vpathc(epath.size()+1);
            for(int e = 0;e<epath.size();e++)
            {
              vpathc(e) = OT(Icc(epath(e)),eend(e));
            }
            vpathc(epath.size()) =
              OT(Icc(epath(epath.size()-1)),1-eend(eend.size()-1));
            assert(vpath.size() == vpathc.size());
            Eigen::Matrix<Scalar ,Eigen::Dynamic ,Eigen::Dynamic> PI(vpath.size(),VT.cols()*2);
            for(int p = 0;p<PI.rows();p++)
            {
              for(int d = 0;d<VT.cols();d++)
              {
                PI(p,          d) = VT( vpath(p),d);
                PI(p,VT.cols()+d) = VT(vpathc(p),d);
              }
            }
            const Scalar bbd = 
              (PI.colwise().maxCoeff() - PI.colwise().minCoeff()).norm();
            Eigen::Matrix<Scalar ,Eigen::Dynamic ,Eigen::Dynamic> UPI,SI;
            Eigen::VectorXi UIc;
            ramer_douglas_peucker(PI,tol*bbd,UPI,UIc,SI);
            UT(vpath,igl::placeholders::all) = SI.leftCols (VT.cols());
            UT(vpathc,igl::placeholders::all) = SI.rightCols(VT.cols());
            for(int i = 0;i<UIc.size()-1;i++)
            {
              vUE.push_back({vpath(UIc(i)),vpath(UIc(i+1))});
            }
            for(int i = 0;i<UIc.size()-1;i++)
            {
              vUE.push_back({vpathc(UIc(i)),vpathc(UIc(i+1))});
            }
          }
        }
        break;
      default:
        assert(false && "Should never reach here");
    }
  }
  list_to_matrix(vUE,UE);
}

#ifdef IGL_STATIC_LIBRARY
// Explicit template instantiation
// generated by autoexplicit.sh
template void igl::straighten_seams<Eigen::Matrix<double, -1, -1, 0, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1>, double, Eigen::Matrix<int, -1, -1, 0, -1, -1>, Eigen::Matrix<double, -1, -1, 1, -1, -1>, Eigen::Matrix<int, -1, -1, 0, -1, -1> >(Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> > const&, Eigen::MatrixBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> > const&, double, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<double, -1, -1, 1, -1, -1> >&, Eigen::PlainObjectBase<Eigen::Matrix<int, -1, -1, 0, -1, -1> >&);
#endif
