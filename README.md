# Welcome to Quake 3 source code with DXR (Ray tracing)!

## What is this
* This is a fork of Quake-III-Arena-Kenny-Edition but with added DXR support.
* DXR is Microsoft's extension to DirectX-12 that adds Ray tracing. 
* If you have a GPU that supports Hardware ray tracing like Nvidia's RTX 20X0 series you can have ray traced Q3A.

![screenshot](https://github.com/myounglcd/Quake-III-Arena-Kenny-Edition-With-DXR/blob/master/screenshots/q3a-raytraced03.png?raw=true)
https://www.youtube.com/watch?v=YGofJni1ZHk

## Caveats
* This was only done as a learning exercise.
* Only simple lighting model is implemented. Single light with shadow and ambient occlusion.
* None of the original material data is used.
* Can't see Hud or console while on.
* Have to enter level through command line (eg. +devmap q3dm7).
* Can't load another level.
* Character animation not working.

## Usage
* Build `visual-studio/quake3.sln` solution.
* Copy q3a asserts into the "solution folder"
* Must start in DX12 mode (+set r_renderAPI 2 )
* In console enter "\dxr_on 1" to turn on.

## Vulkan support 
Vulkan backend has not been touched. 

## Requirements
Windows 10 v1809, "October 2018 Update" (RS5) or later
Windows 10 SDK 10.0.17763.0 or later.
Visual Studio 2017

## Acknowledgements
Original Quake-III-Arena-Kenny-Edition README.md (original-README.md)

Basically started this project by mashing Quake-III-Arena-Kenny-Edition and acmarrs: IntroToDXR (https://github.com/acmarrs/IntroToDXR) together.
There is still a lot of IntroToDXR DNA in here. Thank you for such a great sample.

Microsoft:DirectXShaderCompiler redistributable is included.