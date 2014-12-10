Maya dx11Shader Sample
======================

This sample is the Maya source code for the "C:\Program Files\Autodesk\Maya2015\bin\plug-ins\dx11Shader.mll" plug-in.

Information about dx11Shader in Maya:

  - http://knowledge.autodesk.com/support/maya/learn-explore/caas/CloudHelp/cloudhelp/2015/ENU/Maya/files/GUID-21D2A020-EC76-4679-B38A-D5270CE52566-htm.html
  - http://knowledge.autodesk.com/support/maya/learn-explore/caas/CloudHelp/cloudhelp/2015/ENU/Maya/files/GUID-EE108A86-9830-455E-BF2F-64A4075E8303-htm.html



Dependencies
--------------------
This sample is dependent of the DirectX June 2010 SDK version. 

The D3DX11Effects.lib is a modified version of the DirectX effects component from the DirectX June 2010 SDK version. 

See https://fx11.codeplex.com/license for details.


Building the sample
---------------------------

The sample was created using Visual Studio 2012 Update 4.

  - Load the .sln or .vcxproj in Visual Studio and build either the Hybrid (e.q. Debug) or Release version. There is no Debug Effect component library provided as this time, so use Hybrid whenever you want to build the Debug version of your dx11Shader version.
  - Copy the x64/[Hybrid | Release]/dx11Shader.mll into "C:\Program Files\Autodesk\Maya2015\bin\plug-ins\"
  - Start Maya and load the plug-in in Maya via the loadPlugin command or the Plug-in manager
  

## License

This sample is licensed under the terms of the [MIT License](http://opensource.org/licenses/MIT). Please see the [LICENSE](LICENSE) file for full details.
