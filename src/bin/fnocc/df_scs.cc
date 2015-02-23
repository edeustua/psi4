/*
 *@BEGIN LICENSE
 *
 * PSI4: an ab initio quantum chemistry software package
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *@END LICENSE
 */


#include"psi4-dec.h"
#include<libmints/vector.h>
#include<libmints/matrix.h>
#include<libmints/wavefunction.h>
#include<libqt/qt.h>
#include<sys/times.h>
#include<libciomr/libciomr.h>
#ifdef _OPENMP
    #include<omp.h>
#else
    #define omp_get_wtime() 0.0
    #define omp_get_max_threads() 1
#endif

#include"blas.h"
#include"ccsd.h"
#include<libmints/basisset.h>
#include<libmints/basisset_parser.h>
#include<lib3index/3index.h>

using namespace psi;
using namespace boost;

namespace psi{ namespace fnocc{

void DFCoupledCluster::Local_SCS_MP2(){

  long int v = nvirt;
  long int o = ndoccact;
  long int rs = nmo;
  double ssenergy = 0.0;
  double osenergy = 0.0;

  boost::shared_ptr<PSIO> psio(new PSIO());

  // df (ia|bj) formerly E2klcd
  F_DGEMM('n','t',o*v,o*v,nQ,1.0,Qov,o*v,Qov,o*v,0.0,tempt,o*v);

  SharedMatrix Rii = reference_wavefunction_->CIMTransformationMatrix();
  // the dimension of the Rii transformation matrix:
  int nocc = Rii->colspi()[0];
  double**Rii_pointer = Rii->pointer();
  // transform E2iajb back from quasi-canonical basis
  F_DGEMM('N','T',v*v*o,o,o,1.0,tempt,v*v*o,&Rii_pointer[0][0],nocc,0.0,integrals,v*v*o);

  if (t2_on_disk){
     psio->open(PSIF_DCC_T2,PSIO_OPEN_OLD);
     psio->read_entry(PSIF_DCC_T2,"t2",(char*)&tempv[0],o*o*v*v*sizeof(double));
     psio->close(PSIF_DCC_T2,1);
     tb = tempv;
  }
  // transform t2 back from quasi-canonical basis
  F_DGEMM('N','N',o,v*v*o,o,1.0,&Rii_pointer[0][0],nocc,tb,o,0.0,tempt,o);
  // resort t(abji) -> t(abij)
  for (int ab = 0; ab < v*v; ab++){
      for (int i = 0; i < o; i++){
          for (int j = 0; j < o; j++){
              I1[i*o+j] = tempt[ab*o*o+j*o+i];
          }
      }
      C_DCOPY(o*o,I1,1,tempt+ab*o*o,1);
  }

  // energy
  SharedVector factor = reference_wavefunction_->CIMOrbitalFactors();
  double*factor_pointer = factor->pointer();
  for (int b = o; b < rs; b++){
      for (int a = o; a < rs; a++){
          for (int i = 0; i < o; i++){
              for (int j = 0; j < o; j++){
                  long int iajb = i*v*v*o+(a-o)*v*o+j*v+(b-o);
                  long int ijab = (b-o)*o*o*v+(a-o)*o*o+i*o+j;
                  osenergy += integrals[iajb]*(tempt[ijab])*factor_pointer[i];
                  ssenergy += integrals[iajb]*(tempt[ijab]-tempt[(a-o)*o*o*v+(b-o)*o*o+i*o+j])*factor_pointer[i];
              }
          }
      }
  }
  emp2_os = osenergy;
  emp2_ss = ssenergy;
  emp2 = emp2_os + emp2_ss;

  psio.reset();
}
void DFCoupledCluster::Local_SCS_CCSD(){

  long int v = nvirt;
  long int o = ndoccact;
  long int rs = nmo;
  double ssenergy = 0.0;
  double osenergy = 0.0;

  boost::shared_ptr<PSIO> psio(new PSIO());

  // df (ia|bj) formerly E2klcd
  F_DGEMM('n','t',o*v,o*v,nQ,1.0,Qov,o*v,Qov,o*v,0.0,tempt,o*v);

  SharedMatrix Rii = reference_wavefunction_->CIMTransformationMatrix();
  // the dimension of the Rii transformation matrix:
  int nocc = Rii->colspi()[0];
  double**Rii_pointer = Rii->pointer();
  // transform E2iajb back from quasi-canonical basis
  F_DGEMM('N','T',v*v*o,o,o,1.0,tempt,v*v*o,&Rii_pointer[0][0],nocc,0.0,integrals,v*v*o);

  if (t2_on_disk){
     psio->open(PSIF_DCC_T2,PSIO_OPEN_OLD);
     psio->read_entry(PSIF_DCC_T2,"t2",(char*)&tempv[0],o*o*v*v*sizeof(double));
     psio->close(PSIF_DCC_T2,1);
     tb = tempv;
  }
  // transform t2 back from quasi-canonical basis
  for (int a = 0; a < v; a++){
      for (int b = 0; b < v; b++){
          for (int i = 0; i < o; i++){
              for (int j = 0; j < o; j++){
                  tb[a*o*o*v+b*o*o+i*o+j] += t1[a*o+i]*t1[b*o+j];
              }
          }
      }
  }
  F_DGEMM('N','N',o,v*v*o,o,1.0,&Rii_pointer[0][0],nocc,tb,o,0.0,tempt,o);
  // resort t(abji) -> t(abij)
  for (int ab = 0; ab < v*v; ab++){
      for (int i = 0; i < o; i++){
          for (int j = 0; j < o; j++){
              I1[i*o+j] = tempt[ab*o*o+j*o+i];
          }
      }
      C_DCOPY(o*o,I1,1,tempt+ab*o*o,1);
  }
  for (int a = 0; a < v; a++){
      for (int b = 0; b < v; b++){
          for (int i = 0; i < o; i++){
              for (int j = 0; j < o; j++){
                  tb[a*o*o*v+b*o*o+i*o+j] -= t1[a*o+i]*t1[b*o+j];
              }
          }
      }
  }

  // energy
  SharedVector factor = reference_wavefunction_->CIMOrbitalFactors();
  double*factor_pointer = factor->pointer();
  for (int b = o; b < rs; b++){
      for (int a = o; a < rs; a++){
          for (int i = 0; i < o; i++){
              for (int j = 0; j < o; j++){
                  long int iajb = i*v*v*o+(a-o)*v*o+j*v+(b-o);
                  long int ijab = (b-o)*o*o*v+(a-o)*o*o+i*o+j;
                  osenergy += integrals[iajb]*(tempt[ijab])*factor_pointer[i];
                  ssenergy += integrals[iajb]*(tempt[ijab]-tempt[(a-o)*o*o*v+(b-o)*o*o+i*o+j])*factor_pointer[i];
              }
          }
      }
  }
  eccsd_os = osenergy;
  eccsd_ss = ssenergy;
  eccsd = eccsd_os + eccsd_ss;

  psio.reset();
}

void DFCoupledCluster::SCS_CCSD(){

    long int v  = nvirt;
    long int o  = ndoccact;
    long int rs = nmo;

    double ssenergy = 0.0;
    double osenergy = 0.0;

    // df (ia|bj) formerly E2klcd
    F_DGEMM('n','t',o*v,o*v,nQ,1.0,Qov,o*v,Qov,o*v,0.0,integrals,o*v);

    if (t2_on_disk){
        boost::shared_ptr<PSIO> psio (new PSIO());
        psio->open(PSIF_DCC_T2,PSIO_OPEN_OLD);
        psio->read_entry(PSIF_DCC_T2,"t2",(char*)&tempv[0],o*o*v*v*sizeof(double));
        psio->close(PSIF_DCC_T2,1);
        tb = tempv;
    }

    for (long int a = o; a < rs; a++){
        for (long int b = o; b < rs; b++){
            for (long int i = 0; i < o; i++){
                for (long int j = 0; j < o; j++){

                    long int ijab = (a-o)*v*o*o+(b-o)*o*o+i*o+j;
                    long int iajb = i*v*v*o+(a-o)*v*o+j*v+(b-o);
                    long int jaib = iajb + (i-j)*v*(1-v*o);

                    osenergy += integrals[iajb]*(tb[ijab]+t1[(a-o)*o+i]*t1[(b-o)*o+j]);
                    ssenergy += integrals[iajb]*(tb[ijab]-tb[(b-o)*o*o*v+(a-o)*o*o+i*o+j]);
                    ssenergy += integrals[iajb]*(t1[(a-o)*o+i]*t1[(b-o)*o+j]-t1[(b-o)*o+i]*t1[(a-o)*o+j]);
                }
            }
        }
    }
    eccsd_os = osenergy;
    eccsd_ss = ssenergy;
    eccsd    = eccsd_os + eccsd_ss;
}

void DFCoupledCluster::SCS_MP2(){

    long int v  = nvirt;
    long int o  = ndoccact;
    long int rs = nmo;

    double ssenergy = 0.0;
    double osenergy = 0.0;

    // df (ia|bj) formerly E2klcd
    F_DGEMM('n','t',o*v,o*v,nQ,1.0,Qov,o*v,Qov,o*v,0.0,integrals,o*v);

    if (t2_on_disk){
        boost::shared_ptr<PSIO> psio (new PSIO());
        psio->open(PSIF_DCC_T2,PSIO_OPEN_OLD);
        psio->read_entry(PSIF_DCC_T2,"t2",(char*)&tempv[0],o*o*v*v*sizeof(double));
        psio->close(PSIF_DCC_T2,1);
        tb = tempv;
    }

    for (long int a = o; a < rs; a++){
        for (long int b = o; b < rs; b++){
            for (long int i = 0; i < o; i++){
                for (long int j = 0; j < o; j++){

                    long int ijab = (a-o)*v*o*o+(b-o)*o*o+i*o+j;
                    long int iajb = i*v*v*o+(a-o)*v*o+j*v+(b-o);
                    long int jaib = iajb + (i-j)*v*(1-v*o);
                    osenergy += integrals[iajb]*tb[ijab];
                    ssenergy += integrals[iajb]*(tb[ijab]-tb[(b-o)*o*o*v+(a-o)*o*o+i*o+j]);
                }
            }
        }
    }
    emp2_os = osenergy;
    emp2_ss = ssenergy;
    emp2    = emp2_os + emp2_ss;
}

}}
