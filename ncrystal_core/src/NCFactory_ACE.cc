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

#include "NCrystal/NCFactoryRegistry.hh"
#include "NCrystal/NCMatCfg.hh"
#include "NCrystal/NCLoadACE.hh"

namespace NCrystal {

  //Factory component which can load .ncmat files

  class ACEFactory : public FactoryBase {
  public:
    const char * getName() const { return "stdace"; }

    virtual int canCreateInfo( const MatCfg& cfg ) const {
      return cfg.getDataFileExtension()=="ace" ? 100 : 0;
    }
    virtual const Info * createInfo( const MatCfg& cfg ) const
    {
      return loadACE( cfg.getDataFile().c_str(), cfg.get_temp());
    }
  };

}

//Finally, a function which can be used to enable the above factory. Note that
//this function is forward declared elsewhere or might be dynamically invoked
//(hence the C-mangling), and its name should not be changed just here:

extern "C" void ncrystal_register_ace_factory()
{
  if (!NCrystal::hasFactory("stdace"))
    registerFactory(new NCrystal::ACEFactory);
}
