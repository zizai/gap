#############################################################################
##
#X  Now read the implementation parts from the library.
##

ReadOrComplete( "lib/read5.g" );
ReadOrComplete( "lib/read6.g" );

ReadOrComplete( "lib/read7.g" ); # character theory stuff
ReadOrComplete( "lib/read8.g" ); # overloaded operations, compiler interface
ReadLib( "colorprompt.g"  );


#############################################################################
##
##  Load data libraries
##  The data libraries which may be absent cannot be completed, therefore
##  they must be read in here!

#############################################################################
##
#X  Read library of groups of small order
#X  Read identification routine
##
if TestPackageAvailability("smallgrp")=fail then
  ReadSmall( "readsml.g","small groups" );
fi;

#############################################################################
##
#X  Checking transitive groups library
##
## Assign TransitiveGroupsAvailable to a dummy function to make it
## callable, even if the library is unavailable.
if TestPackageAvailability("transgrp")=fail then
  InstallGlobalFunction(TransitiveGroupsAvailable,deg->false);
fi;

# Only load component if not available as package
# (to be removed together with the `trans` directory)
if TestPackageAvailability("transgrp")=fail then
  TRANS_AVAILABLE:=ReadTrans( "trans.gd","transitive groups" );
  TRANS_AVAILABLE:= TRANS_AVAILABLE and ReadTrans( "trans.grp",
					  "transitive groups" );
  TRANS_AVAILABLE:= TRANS_AVAILABLE and ReadTrans( "trans.gi",
					  "transitive groups" );
fi;

#############################################################################
##
#X  Checking primitive groups library
##
## Assign PrimitiveGroupsAvailable to a dummy function to make it
## callable, even if the library is unavailable.
if TestPackageAvailability("primgrp")=fail then
  InstallGlobalFunction(PrimitiveGroupsAvailable,deg->false);
fi;

# Only load component if not available as package
# (to be removed together with the `prim` directory)
if TestPackageAvailability("primgrp")=fail then
  PRIM_AVAILABLE:=ReadPrim( "primitiv.gd","primitive groups" );
  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "irredsol.gd","irreducible solvable groups" );
  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "primitiv.grp",
                                       "primitive groups" );
  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "primitiv.gi",
                                       "primitive groups" );

  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "irredsol.grp",
                                       "irreducible solvable groups" );
  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "irredsol.gi",
                                       "irreducible solvable groups" );
  PRIM_AVAILABLE:=PRIM_AVAILABLE and ReadPrim( "cohorts.grp",
                                       "irreducible solvable groups" );
fi;

#############################################################################
##
#E

