#ifndef _dx11ConeAngleToHotspotConverter_h_
#define _dx11ConeAngleToHotspotConverter_h_
//-
// Copyright 2012 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license agreement
// provided at the time of installation or download, or which otherwise
// accompanies this software in either electronic or hard copy form.
//+
#include <maya/MPxNode.h>

////////////////////////////////////////////////////////
//
// Maya to OGS Spotlight cone converter node:
//
class dx11ConeAngleToHotspotConverter : public MPxNode
{
public:
                        dx11ConeAngleToHotspotConverter();
    virtual             ~dx11ConeAngleToHotspotConverter(); 

	virtual void		postConstructor( );
    virtual MStatus     compute( const MPlug& plug, MDataBlock& data );

	static  MTypeId		typeId();
    static  void*       creator();
    static  MStatus     initialize();
};

#endif /* _dx11ConeAngleToHotspotConverter_h_ */
