#ifndef _dx11ShaderCmd_h_
#define _dx11ShaderCmd_h_
//-
// ==========================================================================
// Copyright 1995,2006,2008,2011 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
// ==========================================================================
//+

#include <maya/MPxCommand.h>
#include <maya/MPxHardwareShader.h>

class dx11ShaderCmd : MPxCommand
{
public:
	dx11ShaderCmd();
	virtual				~dx11ShaderCmd();

	MStatus				doIt( const MArgList& );
	bool				isUndoable() { return false; }

	static MSyntax		newSyntax();
	static void*		creator();
};

#endif /* _dx11ShaderCmd_h_ */