# VulkanTriangleTemplate

![alt text](https://raw.githubusercontent.com/BDHoefs/VulkanTriangleTemplate/master/Images/Screenshot.jpg)

## About
This is a small template application that renders a rotating triangle with the Vulkan API.
It is intended to be structured so that it can be used as a base for more sophisticated projects
in the future.

It's a little bit more sophisticated than it has to be in order to render a triangle. For example,
it has support for rendering multiple meshes, whose transform matrices are provided through an SSBO
and indexed into with a value contained in a push constant block. It also supports on the fly
swapchain recreation for window resizing, and double-buffering.

It should work on all platforms which support SDL and Vulkan, though it has only been tested on Windows
so far. But any problems getting it up and running should be minor.

## Quick note on building
This project uses submodules to manage its thirdparty dependencies so make sure to 

```
git submodule init
git submodule update
```

Also the shaders are not automatically compiled when the project is built. The resource directory is just
copied to the binary directory, so if you make changes to the shaders make sure to recompile them yourself.

Other than that it's a straightfoward CMake project.
