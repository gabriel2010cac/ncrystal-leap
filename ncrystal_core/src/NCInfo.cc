////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//  This file is part of NCrystal (see https://mctools.github.io/ncrystal/)   //
//                                                                            //
//  Copyright 2015-2019 NCrystal developers                                   //
//                                                                            //
//  Licensed under the Apache License, Version 2.0 (the "License");           //
//  you may not use this file except in compliance with the License.          //
//  You may obtain a copy of the License at                                   //
//                                                                            //
//      http://www.apache.org/licenses/LICENSE-2.0                            //
//                                                                            //
//  Unless required by applicable law or agreed to in writing, software       //
//  distributed under the License is distributed on an "AS IS" BASIS,         //
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  //
//  See the License for the specific language governing permissions and       //
//  limitations under the License.                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#include "NCrystal/NCInfo.hh"
#include "NCLatticeUtils.hh"
#include "NCMath.hh"
#include <algorithm>//for std::stable_sort
#include <set>
#include <cstring>//for memset, memcpy

NCrystal::Info::Info()
  : m_hkl_dlower(-1.0),
    m_hkl_dupper(-2.0),
    m_density(-1.0),
    m_xsect_free(-1.0),
    m_xsect_absorption(-1.0),
    m_temp(-1.0),
    m_debyetemp(-1.0),
    m_xsectprovider(0),
    m_lock(false)
{
  static uint64_t next_unique_id = 1;//safe to assume uint64_t never overflows
  m_uniqueid = next_unique_id++;
  std::memset(&m_structinfo,0,sizeof(m_structinfo));
  m_structinfo.spacegroup = 999999;//unset
}

NCrystal::Info::~Info()
{
  delete m_xsectprovider;
}

#if __cplusplus >= 201103L
NCrystal::HKLInfo& NCrystal::HKLInfo::operator=(NCrystal::HKLInfo &&o)
{
  if (&o==this) {
    //guard against self-move-assignment (seen on GCC4.4.7 during sort
    //where delete[] eqv_hkl gave problems).
    return *this;
  }
  dspacing = o.dspacing;
  fsquared = o.fsquared;
  fsquared_LEAPR = o.fsquared_LEAPR;
  h = o.h;
  k = o.k;
  l = o.l;
  multiplicity = o.multiplicity;
  demi_normals.clear();
  std::swap(demi_normals,o.demi_normals);
  delete[] eqv_hkl;
  eqv_hkl = 0;
  std::swap(eqv_hkl,o.eqv_hkl);
  return *this;
}
NCrystal::HKLInfo::HKLInfo(const NCrystal::HKLInfo &&o)
  : eqv_hkl(0)
{
  *this = o;
}
#endif

NCrystal::HKLInfo& NCrystal::HKLInfo::operator=(const NCrystal::HKLInfo &o)
{
  if (&o==this) {
    //guard against self-assignment (seen on GCC4.4.7 during sort
    //where delete[] eqv_hkl gave problems).
    return *this;
  }
  dspacing = o.dspacing;
  fsquared = o.fsquared;
  fsquared_LEAPR = o.fsquared_LEAPR;
  h = o.h;
  k = o.k;
  l = o.l;
  multiplicity = o.multiplicity;
  demi_normals = o.demi_normals;
  delete[] eqv_hkl;
  eqv_hkl = 0;
  if (o.eqv_hkl) {
    nc_assert(!o.demi_normals.empty());
    size_t n = o.demi_normals.size()*3;
    nc_assert(n);
    eqv_hkl = new short[n];
    std::memcpy(eqv_hkl,o.eqv_hkl,n*sizeof(short));
  }
  return *this;
}

namespace NCrystal {
  bool dhkl_compare( const NCrystal::HKLInfo& rh, const NCrystal::HKLInfo& lh )
  {
    if( ncabs(lh.dspacing-rh.dspacing) > 1.0e-6 )
      return lh.dspacing < rh.dspacing;
    else if( ncabs(lh.fsquared*lh.multiplicity - rh.fsquared*rh.multiplicity) > 1.0e-6 )
      return lh.fsquared*lh.multiplicity < rh.fsquared*rh.multiplicity;
    else if (lh.multiplicity != rh.multiplicity)
      return lh.multiplicity < rh.multiplicity;
    else if( lh.h!=rh.h )
      return rh.h < lh.h;
    else if( lh.k!=rh.k )
      return rh.k < lh.k;
    return rh.l < lh.l;
  }
  bool atominfo_compare( const NCrystal::AtomInfo& rh, const NCrystal::AtomInfo& lh )
  {
    if(rh.atomic_number == lh.atomic_number)
      NCRYSTAL_THROW(LogicError,"Invalid AtomInfo: Same Z encountered more than once in list.");
    if (rh.atomic_number != lh.atomic_number)
      return rh.atomic_number < lh.atomic_number;
    return rh.number_per_unit_cell < lh.number_per_unit_cell;
  }
  bool atominfo_pos_compare( const NCrystal::AtomInfo::Pos& rh, const NCrystal::AtomInfo::Pos& lh )
  {
    if (rh.x!=lh.x) return rh.x < lh.x;
    if (rh.y!=lh.y) return rh.y < lh.y;
    return rh.z < lh.z;
  }
  bool atominfo_pos_compare_yfirst( const NCrystal::AtomInfo::Pos& rh, const NCrystal::AtomInfo::Pos& lh )
  {
    if (rh.y!=lh.y) return rh.y < lh.y;
    if (rh.x!=lh.x) return rh.x < lh.x;
    return rh.z < lh.z;
  }
  bool atominfo_pos_compare_zfirst( const NCrystal::AtomInfo::Pos& rh, const NCrystal::AtomInfo::Pos& lh )
  {
    if (rh.z!=lh.z) return rh.z < lh.z;
    if (rh.y!=lh.y) return rh.y < lh.y;
    return rh.x < lh.x;
  }
  void atominfo_pos_remap( double& x ) {
    const double xorig = x;
    if ( x<0.0 )
      x += 1.0;
    else if ( x>=1.0 )
      x -= 1.0;
    if ( ! (x>=0 && x<1.0) )//must be in [0,1) and not NaN
      NCRYSTAL_THROW2(BadInput,"Invalid coordinate of atom position encountered (out of range or NaN): "<<xorig);
    if (x==0.0)
      x=0.0;//remaps -0 to 0
  }
  void detect_duplicate_positions(  std::vector<AtomInfo::Pos>& plist ) {
    const double pos_tolerance = 0.01;
    for (std::size_t i = 1; i < plist.size(); ++i) {
      const AtomInfo::Pos& p1 = plist.at(i-1);
      const AtomInfo::Pos& p2 = plist.at(i);
      if ( ncabs(p1.x-p2.x)<pos_tolerance && ncabs(p1.y-p2.y)<pos_tolerance && ncabs(p1.z-p2.z)<pos_tolerance )
        NCRYSTAL_THROW2(BadInput,"The same atom position used more than once: ("<<p1.x<<", "<<p1.y<<", "<<p1.z<<")");
    }
  }
}

void NCrystal::Info::objectDone()
{
  //TODO for NC2: Throw LogicErrors or BadInput here?
  ensureNoLock();
  m_lock=true;

  //avoid excess memory usage in hkl list:
  nc_assert_always(m_hkllist.empty()||hasHKLInfo());
  if (m_hkllist.size()!=m_hkllist.capacity())
    HKLList(m_hkllist.begin(),m_hkllist.end()).swap(m_hkllist);

  //sort lists:
  std::stable_sort(m_hkllist.begin(),m_hkllist.end(),dhkl_compare);
  std::stable_sort(m_atomlist.begin(),m_atomlist.end(),atominfo_compare);

  //check that per-element debye temp, positions and MSD's are consistently
  //specified (either all or none must have):
  std::set<unsigned> z_seen;
  std::vector<AtomInfo::Pos> all_positions;
  all_positions.reserve(128);

  unsigned ntotatoms_from_atominfo(0);

  AtomList::iterator itAtm(m_atomlist.begin()), itAtmE(m_atomlist.end());
  for (;itAtm!=itAtmE;++itAtm)
  {
    //Map all atom positions to interval [0,1) (e.g. 1.0 becomes 0.0, -0.3
    //becomes 0.7, etc.). However at most move 1.0:
    std::vector<AtomInfo::Pos>::iterator itPos(itAtm->positions.begin()), itPosE(itAtm->positions.end());
    for (;itPos!=itPosE;++itPos) {
      atominfo_pos_remap(itPos->x);
      atominfo_pos_remap(itPos->y);
      atominfo_pos_remap(itPos->z);
      all_positions.push_back(*itPos);
    }
    //Sort positions, ensuring a well defined ordering:
    std::stable_sort(itAtm->positions.begin(),itAtm->positions.end(),atominfo_pos_compare);

    if (!itAtm->number_per_unit_cell)
      NCRYSTAL_THROW(BadInput,"atominfo found for element with number_per_unit_cell=0!.");

    ntotatoms_from_atominfo += itAtm->number_per_unit_cell;

    //per-element debye temps should be 0 (indicating absence) or have a sane value between 1K and 100000K.
    if ( ! (itAtm->debye_temp==0.0 || (itAtm->debye_temp>1.0&&itAtm->debye_temp<100000.0) ) )
      NCRYSTAL_THROW(LogicError,"Inconsistency: per-element Debye temperatures with invalid values encountered.");
    if ( bool(itAtm->debye_temp>0) != bool(m_atomlist.front().debye_temp>0) )
      NCRYSTAL_THROW(LogicError,"Inconsistency: per-element Debye temperatures specified for some but not all elements.");
    if ( itAtm->positions.empty() != m_atomlist.front().positions.empty() )
      NCRYSTAL_THROW(LogicError,"Inconsistency: positions specified for some but not all elements.");
    if ( !itAtm->positions.empty() && itAtm->positions.size()!=itAtm->number_per_unit_cell)
      NCRYSTAL_THROW(LogicError,"Inconsistency: inconsistency between length of positions vector and number_per_unit_cell");
    if ( ! ( itAtm->mean_square_displacement >= 0.0 ) )//will catch NaN as well
      NCRYSTAL_THROW(LogicError,"Inconsistency: mean_square_displacement must be >= 0.0 and not NaN");
    if ( bool(itAtm->mean_square_displacement>0) != bool(m_atomlist.front().mean_square_displacement>0) )
      NCRYSTAL_THROW(LogicError,"Inconsistency: mean_square_displacement specified for some but not all elements.");
    if (z_seen.count(itAtm->atomic_number))
      NCRYSTAL_THROW2(LogicError,"Inconsistency: AtomInfo for Z="<<itAtm->atomic_number<<" specified more than once");
  }

  //Ensure only one atom exists at a given position, within a tolerance. To make
  //sure this works we sort three times after x, y and z coordinates
  //respectively, and compare neighbouring elements each time:
  std::sort(all_positions.begin(),all_positions.end(),atominfo_pos_compare);
  detect_duplicate_positions(all_positions);
  std::sort(all_positions.begin(),all_positions.end(),atominfo_pos_compare_yfirst);
  detect_duplicate_positions(all_positions);
  std::sort(all_positions.begin(),all_positions.end(),atominfo_pos_compare_zfirst);
  detect_duplicate_positions(all_positions);

  //Check that hkl normal information is self-consistent:
  nc_assert_always(sizeof(HKLInfo::Normal)==3*sizeof(double));

  HKLList::iterator ithkl = m_hkllist.begin();
  HKLList::iterator ithklE = m_hkllist.end();
  int has_demi_normals(-1);
  int has_eqv_hkl(-1);
  for (;ithkl!=ithklE;++ithkl) {
    if (! ithkl->demi_normals.empty() ) {
      if ( ithkl->demi_normals.size() != ithkl->demi_normals.capacity() ) {
        //Remove over-capacity:
        std::vector<HKLInfo::Normal>(ithkl->demi_normals.begin(),ithkl->demi_normals.end()).swap(ithkl->demi_normals);
      }
      if (has_demi_normals==0)
        NCRYSTAL_THROW(LogicError,"Inconsistency: Some but not all HKLInfo objects provide demi_normals");
      has_demi_normals=1;
      if (ithkl->multiplicity < 1 || ithkl->multiplicity > 99999)
        NCRYSTAL_THROW(LogicError,"HKL multiplicity is not in range 1..99999");
      if (ithkl->demi_normals.size()*2 != (size_t)ithkl->multiplicity)
        NCRYSTAL_THROW(LogicError,"HKL normals provided but number does not match multiplicity");

      if (has_eqv_hkl!=-1 && (ithkl->eqv_hkl?1:0)!=has_eqv_hkl )
        NCRYSTAL_THROW(LogicError,"Inconsistency: Some but not all HKLInfo objects provide eqv_hkl");
      has_eqv_hkl = (ithkl->eqv_hkl?1:0);
      //check demi-normals are normalised:
      std::vector<HKLInfo::Normal>::const_iterator itN, itNE = ithkl->demi_normals.end();
      for (itN = ithkl->demi_normals.begin();itN!=itNE;++itN) {
        double n2 = itN->x*itN->x + itN->y*itN->y + itN->z*itN->z;
        if (ncabs(n2-1.0)>1.0e-6)
          NCRYSTAL_THROW(BadInput,"Provided demi_normals must have unit lengths");
      }
    } else {
      if (has_demi_normals==1)
        NCRYSTAL_THROW(LogicError,"Inconsistency: Some but not all HKLInfo objects provide demi_normals");
      has_demi_normals=0;
      if (ithkl->eqv_hkl)
        NCRYSTAL_THROW(LogicError,"eqv_hkl provided although demi_normals are not!");
    }
    if ( ithkl->eqv_hkl && ithkl->multiplicity%2 != 0 )
      NCRYSTAL_THROW(LogicError,"Expanded HKL info (eqv_hkl) provided, but multiplicity is not an even number.");
  }

  if (hasStructureInfo()) {
    if ( ! (m_structinfo.volume > 0.0) )
      NCRYSTAL_THROW2(BadInput,"StructureInfo volume not a positive number: "<<m_structinfo.volume);
    if ( ! (m_structinfo.n_atoms > 0.0) )
      NCRYSTAL_THROW2(BadInput,"StructureInfo n_atoms not a positive number: "<<m_structinfo.n_atoms);
    if ( hasAtomInfo() && m_structinfo.n_atoms != ntotatoms_from_atominfo )
      NCRYSTAL_THROW2(BadInput,"Inconsistent total number of atoms deduced from StructureInfo and AtomInfo ("
                      <<m_structinfo.n_atoms<<" vs. "<<ntotatoms_from_atominfo<<")");

    checkAndCompleteLattice( m_structinfo.spacegroup, m_structinfo.lattice_a,
                             m_structinfo.lattice_b, m_structinfo.lattice_c );

    if ( ! ( m_structinfo.alpha > 0 && m_structinfo.alpha < 180 &&
             m_structinfo.beta  > 0 && m_structinfo.beta  < 180 &&
             m_structinfo.gamma > 0 && m_structinfo.gamma < 180 ) ) {
      NCRYSTAL_THROW(BadInput,"Lattice angles must all be >0 and <180 degrees.");
    }

    if ( ! ( m_structinfo.alpha > kPi || m_structinfo.beta >kPi || m_structinfo.gamma > kPi ) ) {
      NCRYSTAL_THROW(BadInput,"Lattice angles in radians where degrees was expected.");
    }

  }

  if (hasXSectFree()) {
    nc_assert_always(getXSectFree()>=0.0&&getXSectFree()<1e20);
  }

  if (hasXSectAbsorption()) {
    nc_assert_always(getXSectAbsorption()>=0.0&&getXSectAbsorption()<1e20);
  }

  if (hasTemperature()) {
    nc_assert_always(getTemperature()>0.0&&getTemperature()<1e6);
  }

  if (hasDebyeTemperature()) {
    nc_assert_always(getDebyeTemperature()>0.0&&getDebyeTemperature()<1e6);
  }

  if (hasHKLInfo()) {
    nc_assert_always( m_hkl_dlower < m_hkl_dupper);
    nc_assert_always( m_hkl_dlower > 0.0 );
    nc_assert_always( m_hkl_dupper > 0.0 );
  }

  if ( hasDensity() ) {
    nc_assert_always(m_density>0.0);
  }
}

void NCrystal::Info::enableHKLInfo(double dlower, double dupper)
{
  ensureNoLock();
  m_hkl_dlower = dlower;
  m_hkl_dupper = dupper;
  nc_assert_always(hasHKLInfo());
}

void NCrystal::Info::ensureNoLock()
{
  if (m_lock)
    NCRYSTAL_THROW(LogicError,"Modification of Info object after it is locked is forbidden");
}

NCrystal::HKLList::const_iterator NCrystal::Info::searchExpandedHKL(short h, short k, short l) const
{
  nc_assert_always(hasHKLInfo());
  nc_assert_always(hasExpandedHKLInfo());

  HKLList::const_iterator it(hklBegin()), itE(hklEnd());
  for(;it!=itE;++it)
  {
    for(unsigned i=0;i < it->multiplicity/2;i++)
    {
      nc_assert(it->eqv_hkl);
      if( (it->eqv_hkl[i*3]==h && it->eqv_hkl[i*3+1]==k && it->eqv_hkl[i*3+2]==l) ||
          (it->eqv_hkl[i*3]==-h && it->eqv_hkl[i*3+1]==-k && it->eqv_hkl[i*3+2]==-l) )
      {
        return it;
      }
    }
  }
  return itE;
}
//TODO for NC2: why not always provide eqv_hkl from .ncmat factories and remove
//demi_normals from the interface? The memory usage is 4 times lower and the
//added initialisation time is likely negligible.


double NCrystal::Info::hklDMinVal() const
{
  nc_assert(hasHKLInfo());
  if (m_hkllist.empty())
    return kInf;
  return hklLast()->dspacing;
}

double NCrystal::Info::hklDMaxVal() const
{
  nc_assert(hasHKLInfo());
  if (m_hkllist.empty())
    return kInf;
  return hklBegin()->dspacing;
}

double NCrystal::Info::dspacingFromHKL( int h, int k, int l ) const
{
  if (!hasStructureInfo())
    NCRYSTAL_THROW(MissingInfo,"Info object lacks Structure information.");
  const StructureInfo & si = getStructureInfo();
  RotMatrix rec_lat = getReciprocalLatticeRot( si.lattice_a, si.lattice_b, si.lattice_c,
                                               si.alpha*kDeg, si.beta*kDeg, si.gamma*kDeg );
  return NCrystal::dspacingFromHKL( h,k,l, rec_lat );
}
