#############################################################################
##
#W  csetgrp.gd                      GAP library              Alexander Hulpke
##
#H  @(#)$Id$
##
#Y  Copyright (C)  1996,  Lehrstuhl D fuer Mathematik,  RWTH Aachen,  Germany
##
##  This file contains the declarations of operations for cosets.
##
Revision.csetgrp_gd:=
  "@(#)$Id$";

#############################################################################
##
#V  InfoCoset
##
InfoCoset := NewInfoClass ("InfoCoset");

#############################################################################
##
#F  AscendingChain(<G>,<U>) . . . . . . .  chain of subgroups G=G_1>...>G_n=U
##
AscendingChain := NewOperationArgs("AscendingChain");

#############################################################################
##
#O  AscendingChainOp(<G>,<U>)  chain of subgroups
##
AscendingChainOp := NewOperation("AscendingChainOp",[IsGroup,IsGroup]);

#############################################################################
##
#A  ComputedAscendingChains     list of already computed ascending chains
##
ComputedAscendingChains := NewAttribute("ComputedAscendingChains",IsGroup,
                                        "mutable");

#############################################################################
##
#O  CanonicalRightCosetElement(U,g)    canonical representative of U*g 
##                                  (Representation dependent!)
##
CanonicalRightCosetElement:=NewOperation("CanonicalRightCosetElement",
  [IsGroup,IsObject]);

#############################################################################
##
#C  IsDoubleCoset
##
IsDoubleCoset := NewCategory("IsDoubleCoset",
    IsDomain and IsExtLSet and IsExtRSet);

#############################################################################
##
#O  DoubleCoset
##
DoubleCoset:=NewOperation("DoubleCoset",[IsGroup,IsObject,IsGroup]);
#T 1997/01/16 fceller was old 'NewConstructor'


#############################################################################
##
#O  DoubleCosets
##
DoubleCosets := NewOperationArgs("DoubleCosets");

#############################################################################
##
#O  DoubleCosetsNC
##
DoubleCosetsNC := NewOperation("DoubleCosetsNC",[IsGroup,IsGroup,IsGroup]);

#############################################################################
##
#A  RepresentativesContainedRightCosets(<D>)
##
RepresentativesContainedRightCosets := NewAttribute(
  "RepresentativesContainedRightCosets", IsDoubleCoset );

#############################################################################
##
#C  IsRightCoset
##
IsRightCoset := NewCategory("IsRightCoset",
    IsDomain and IsExternalSet);

#############################################################################
##
#O  RightCoset
##
RightCoset:=NewOperation("RightCoset",[IsGroup,IsObject]);
#T 1997/01/16 fceller was old 'NewConstructor'


#############################################################################
##
#O  RightCosets
##
RightCosets := NewOperationArgs("RightCosets");

#############################################################################
##
#O  RightCosetsNC
##
RightCosetsNC := NewOperation("RightCosetsNC",[IsGroup,IsGroup]);

#############################################################################
##
#A  RightCosetsDefaultType
##
RightCosetsDefaultType := NewAttribute("RightCosetsDefaultType",IsFamily);

#############################################################################
##
#A  DoubleCosetsDefaultType
##
DoubleCosetsDefaultType := NewAttribute("DoubleCosetsDefaultType",IsFamily);

#############################################################################
##
#F  IntermediateGroup(<G>,<U>)  . . . . . . . . . subgroup of G containing U
##
##  This routine tries to find a subgroup E of G, such that G>E>U. If U is
##  maximal, it returns false. This is done by finding minimal blocks for
##  the operation of G on the Right Cosets of U.
##
IntermediateGroup := NewOperationArgs("IntermediateGroup");

#############################################################################
##
#E  csetgrp.gd  . . . . . . . . . . . . . . . . . . . . . . . . . . ends here
##
