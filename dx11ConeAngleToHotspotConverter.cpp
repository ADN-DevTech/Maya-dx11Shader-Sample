//-
// Copyright 2011 Autodesk, Inc.  All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk license
// agreement provided at the time of installation or download, or which
// otherwise accompanies this software in either electronic or hard copy form.
//+

#if _MSC_VER >= 1700
#pragma warning( disable: 4005 )
#endif


#include "dx11ConeAngleToHotspotConverter.h"
#include "dx11Shader.h"
#include "dx11ShaderStrings.h"

#include <maya/MFnNumericAttribute.h>
#include <maya/MFnUnitAttribute.h>

#include <maya/MString.h> 
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MAngle.h>
#include <maya/MDataBlock.h>
#include <maya/MDataHandle.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDag.h>

#define McheckErr(stat,stringId)								\
    if ( MS::kSuccess != stat ) {								\
		MString msg = dx11ShaderStrings::getString(stringId);	\
        stat.perror(msg);										\
        return MS::kFailure;									\
    }

static MObject sConeAngle; 
static MObject sPenumbraAngle;  
static MObject sHotspot;  
static MObject sFalloff;   

dx11ConeAngleToHotspotConverter::dx11ConeAngleToHotspotConverter() {}
dx11ConeAngleToHotspotConverter::~dx11ConeAngleToHotspotConverter() {}

void dx11ConeAngleToHotspotConverter::postConstructor( ) {
	setExistWithoutOutConnections(false);
}

MStatus dx11ConeAngleToHotspotConverter::compute( const MPlug& plug, MDataBlock& data )
{
    
    MStatus returnStatus;
 
    if ((plug == sHotspot) || (plug.parent() == sHotspot)) {
        MDataHandle coneAngleData = data.inputValue( sConeAngle, &returnStatus );
        McheckErr(returnStatus,dx11ShaderStrings::kErrorConeAngle);

        MDataHandle forwardData = data.inputValue( sPenumbraAngle, &returnStatus );
        McheckErr(returnStatus,dx11ShaderStrings::kErrorPenumbraAngle);

		double coneAngle = coneAngleData.asAngle().asRadians() / 2.0;
		double penumbraAngle = coneAngleData.asAngle().asRadians() / 2.0;
		double outputAngle = coneAngle;
		if (penumbraAngle < 0)
			outputAngle += penumbraAngle;
        
        MDataHandle outputRot = data.outputValue( sHotspot );
        outputRot.set( outputAngle );
        outputRot.setClean();
	}
    else if ((plug == sFalloff) || (plug.parent() == sFalloff)) {
        MDataHandle coneAngleData = data.inputValue( sConeAngle, &returnStatus );
        McheckErr(returnStatus,dx11ShaderStrings::kErrorConeAngle);

        MDataHandle penumbraAngleData = data.inputValue( sPenumbraAngle, &returnStatus );
        McheckErr(returnStatus,dx11ShaderStrings::kErrorPenumbraAngle);

		double coneAngle = coneAngleData.asAngle().asRadians() / 2.0;
		double penumbraAngle = penumbraAngleData.asAngle().asRadians() / 2.0;
		double outputAngle = coneAngle;
		if (penumbraAngle > 0)
			outputAngle += penumbraAngle;
        
        MDataHandle outputRot = data.outputValue( sFalloff );
        outputRot.set( outputAngle );
        outputRot.setClean();
    } else
        return MS::kUnknownParameter;

    return MS::kSuccess;
}

/* static */
MTypeId	dx11ConeAngleToHotspotConverter::typeId()
{
	// This typeid must be unique across the universe of Maya plug-ins.
	// The typeid is a unique 32bit indentifier that describes this node.
	// It is used to save and retrieve nodes of this type from the binary
	// file format.  If it is not unique, it will cause file IO problems.
	static MTypeId sId( 0x00081055 );
	return sId;
}

void* dx11ConeAngleToHotspotConverter::creator()
{
    return new dx11ConeAngleToHotspotConverter();
}

MStatus dx11ConeAngleToHotspotConverter::initialize()
{
    MFnNumericAttribute nAttr;
    MFnUnitAttribute    uAttr;
    MStatus             stat;

    // Set up inputs
    //
    sConeAngle = uAttr.create( "coneAngle", "ca", MFnUnitAttribute::kAngle, 45.0 );
        uAttr.setStorable(false);
        uAttr.setConnectable(true);
    sPenumbraAngle = uAttr.create( "penumbraAngle", "pa", MFnUnitAttribute::kAngle, 0.0 );
        uAttr.setStorable(false);
        uAttr.setConnectable(true);

    // Set up outputs
    // 
    sHotspot = nAttr.create( "hotspot", "hs", MFnNumericData::kDouble );
        nAttr.setStorable(false);
        nAttr.setConnectable(true);
    sFalloff = nAttr.create( "falloff", "fo", MFnNumericData::kDouble );
        nAttr.setStorable(false);
        nAttr.setConnectable(true);

    stat = addAttribute( sConeAngle );
        if (!stat) { stat.perror("addAttribute"); return stat;}
    stat = addAttribute( sPenumbraAngle );
        if (!stat) { stat.perror("addAttribute"); return stat;}
    stat = addAttribute( sHotspot );
        if (!stat) { stat.perror("addAttribute"); return stat;}
    stat = addAttribute( sFalloff );
        if (!stat) { stat.perror("addAttribute"); return stat;}

    stat = attributeAffects( sConeAngle, sHotspot );
        if (!stat) { stat.perror("attributeAffects"); return stat;}
    stat = attributeAffects( sConeAngle, sFalloff );
        if (!stat) { stat.perror("attributeAffects"); return stat;}
    stat = attributeAffects( sPenumbraAngle, sHotspot );
        if (!stat) { stat.perror("attributeAffects"); return stat;}
    stat = attributeAffects( sPenumbraAngle, sFalloff );
        if (!stat) { stat.perror("attributeAffects"); return stat;}

    return MS::kSuccess;
}

