//-
// ==========================================================================
// Copyright 1995,2006,2008 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================
//+ 

#if _MSC_VER >= 1700
#pragma warning( disable: 4005 )
#endif

#include "dx11Shader.h"
#include "dx11ShaderCmd.h"
#include "dx11ShaderOverride.h"
#include "dx11ShaderStrings.h"
#include "dx11ConeAngleToHotspotConverter.h"
#include "crackFreePrimitiveGenerator.h"

#include <maya/MFnPlugin.h>
#include <maya/MIOStream.h>
#include <maya/MGlobal.h>

#include <maya/MHWShaderSwatchGenerator.h>
#include <maya/MHardwareRenderer.h>

#include <maya/MDrawRegistry.h>

static const MString sDX11ShaderRegistrantId("DX11ShaderRegistrantId");

MStatus initializePlugin( MObject obj )
//
//	Description:
//		this method is called when the plug-in is loaded into Maya.  It
//		registers all of the services that this plug-in provides with
//		Maya.
//
//	Arguments:
//		obj - a handle to the plug-in object (use MFnPlugin to access it)
//
{
	MFnPlugin plugin( obj, "Autodesk", "1.0", MApiVersion );
	MString UserClassify = MString( "shader/surface/utility:drawdb/shader/surface/dx11Shader" );

	// Register string resources
	//
	CHECK_MSTATUS( plugin.registerUIStrings( dx11ShaderStrings::registerMStringResources, "dx11ShaderInitStrings" ) );

	// Don't initialize swatches in batch mode
	if (MGlobal::mayaState() != MGlobal::kBatch)
	{
		static MString swatchName("dx11ShaderRenderSwatchGen");
		MSwatchRenderRegister::registerSwatchRender(swatchName, MHWShaderSwatchGenerator::createObj );
		UserClassify = MString( "shader/surface/utility/:drawdb/shader/surface/dx11Shader:swatch/"+swatchName );
	}

	// Run MEL script for user interface initialization.
	if (MGlobal::mayaState() == MGlobal::kInteractive)
	{
		MString sCmd = "evalDeferred \"source \\\"dx11Shader_initUI.mel\\\"\"";
		MGlobal::executeCommand( sCmd );
	}

	CHECK_MSTATUS( plugin.registerNode("dx11Shader",
		dx11ShaderNode::typeId(),
		dx11ShaderNode::creator,
		dx11ShaderNode::initialize,
		MPxNode::kHardwareShader,
		&UserClassify));

	CHECK_MSTATUS( plugin.registerNode( "coneAngleToHotspotConverter", 
		dx11ConeAngleToHotspotConverter::typeId(), 
        dx11ConeAngleToHotspotConverter::creator, 
		dx11ConeAngleToHotspotConverter::initialize ));

	CHECK_MSTATUS( plugin.registerCommand("dx11Shader",
		dx11ShaderCmd::creator,
		dx11ShaderCmd::newSyntax));

	// Register a shader override for this node
	CHECK_MSTATUS(
		MHWRender::MDrawRegistry::registerShaderOverrideCreator(
			"drawdb/shader/surface/dx11Shader",
			sDX11ShaderRegistrantId,
			dx11ShaderOverride::Creator));

	// Register the vertex mutators with Maya
	//
	CHECK_MSTATUS(
		MHWRender::MDrawRegistry::registerIndexBufferMutator("PNAEN18", CrackFreePrimitiveGenerator::createCrackFreePrimitiveGenerator18));

	CHECK_MSTATUS(
		MHWRender::MDrawRegistry::registerIndexBufferMutator("PNAEN9", CrackFreePrimitiveGenerator::createCrackFreePrimitiveGenerator9));

	// Add and manage default plugin user pref:
	MGlobal::executeCommandOnIdle("dx11ShaderCreateUI");
	
	// Register dx11Shader to filePathEditor
	MStatus status = MGlobal::executeCommand("filePathEditor -registerType \"dx11Shader.shader\" -typeLabel \"DX11Shader\" -temporary");
    if (!status) {
		MString nodeAttr("dx11Shader.shader");
		MString errorString = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorRegisterNodeType, nodeAttr );
		MGlobal::displayWarning( errorString );
    }

	return MStatus::kSuccess;
}

MStatus uninitializePlugin( MObject obj)
//
//	Description:
//		this method is called when the plug-in is unloaded from Maya. It
//		deregisters all of the services that it was providing.
//
//	Arguments:
//		obj - a handle to the plug-in object (use MFnPlugin to access it)
//
{
	MStatus   status;
	MFnPlugin plugin( obj );

	// Deregister our node types.
	//
	CHECK_MSTATUS( plugin.deregisterCommand( "dx11Shader" ) );
	CHECK_MSTATUS( plugin.deregisterNode( dx11ShaderNode::typeId() ));
    CHECK_MSTATUS( plugin.deregisterNode( dx11ConeAngleToHotspotConverter::typeId() ));

	// Deregister the shader override
	//
	CHECK_MSTATUS(
		MHWRender::MDrawRegistry::deregisterShaderOverrideCreator(
			"drawdb/shader/surface/dx11Shader",
			sDX11ShaderRegistrantId));

	// Deregister the vertex mutators
	//
	CHECK_MSTATUS(MHWRender::MDrawRegistry::deregisterIndexBufferMutator("PNAEN18"));
	CHECK_MSTATUS(MHWRender::MDrawRegistry::deregisterIndexBufferMutator("PNAEN9"));

	// Remove user pref UI:
	MGlobal::executeCommandOnIdle("dx11ShaderDeleteUI");
	
	// Deregister dx11Shader from filePathEditor
	status = MGlobal::executeCommand("filePathEditor -deregisterType \"dx11Shader.shader\" -temporary");
    if (!status) {
		MString nodeType("dx11Shader.shader");
		MString errorString = dx11ShaderStrings::getString( dx11ShaderStrings::kErrorDeregisterNodeType, nodeType );
		MGlobal::displayWarning( errorString );
    }

	return MStatus::kSuccess;
}


