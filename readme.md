# UWP BLE packed as C++ winrt dll

This VisualStudio-project compiles to a C++-dll that can be imported into Unity. With this dll there is no need to create a UWP anymore to use BLE on Windows. Also no conditional compiling is needed. You can even use Unity Editor's play-button with this dll. 

The Unity Editor is working with .NET-Framework but Microsoft's BLE API is only usable with .NET-Core. There is a C#-wrapper for .NET-Core functions, but these wrappers don't work in Unity for Unity-specific reasons. You can find some details in [this blog-post by Mike Taulty](https://mtaulty.com/2019/03/22/rough-notes-on-experiments-with-uwp-apis-in-the-unity-editor-with-c-winrt/). The blog-post also motivated me during the development of this dll.

With Unity's future integration of C# version 5 this repo most probably should become obsolete as C# version 5 is meant to merge .NET-Framework and .NET-Core so you should be able to call Windows' BLE API directly from C# then. Still this repo might be useful if you want to use BLE earlier. Furthermore the dll's API is strongly leaned on Windows' own API, actually it is only a wrapper (plus some subtle thread handling). So switching to C# 5 later shouldn't be much work.

## Build

You have to build this project in VisualStudio. Follow these steps:

- Open the file BleWinrtDll.sln with VisualStudio (tested with Community 2019).
- Choose configuration "Release" and "x64" (I think it must match your machine architecture).
- In the project explorer, right-click the project "BleWinrtDll" and choose "Compile".
- Wait until the compilation finishes successfully.
- The End.

Now you find the file `BleWinrtDll.dll` in the folder `BleWinrtDll/x64/Release`. You can import this dll into your Unity-project. To try it out, you can copy the file into the `DebugBle` folder (replacing the existing file) and start the DebugBle project. If your computer has bluetooth enabled, you should see some scanned bluetooth devices. If you modify the file `DebugBle/Program.cs` and change the device name, service UUID and characteristic UUIDs to match your specific BLE device, you should also receive some packages from your BLE device.