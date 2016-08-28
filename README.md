# Kafka

> **WARNING** This only works on a subset of recent AMD GPUs (others might work, but are untested). Kafka was developed on an AMD A6 APU with both an R5 200-series and R5 engineering sample. Only one display must be connected, or external ones switched to exclusive.

This is a walkthrough of how to create the smallest possible framework for 1k democoding. At the end of this, we'll have an OpenGL context displaying a shader in just under 520 bytes. 

## What you need

- A C++ compiler. I'll use MSVC here. Kafka uses a single `*.cpp` file, so Visual Studio (or a VS solution) is not necessary.
- The Windows SDK.
- Crinkler (download and put `crinkler.exe` in your project directory, renamed to `link.exe`).
- Basic C/++ knowledge and quite a bit Assembly knowledge.

The idea here is to prove that one can create extremely small demos without writing the whole thing in pure Assembly. The basic skill is to know how compilers work in order to write C/++ code that produces the Assembly *you* want. Inline Assembly is only used when

1. we know the compiler will make a mistake<sup>1</sup> or
2. it is inevitable, because we need to get rid of one more byte.

<sup>1</sup> - "Mistake" means the compiler generates overhead in the Assembly (type checks, overflow handling etc.)

## Macros

We only need these macros:

```cpp
#define RUNTIME 3
#define native(t) __declspec(dllimport) t __stdcall
#define GLExt(a) a(wglGetProcAddress(#a))
```

- `RUNTIME` is the demo runtime in multiples of 64 seconds. Because we need to avoid WinAPI overhead, we abuse some OpenGL functions to pass the current time to the main shader. However, this means we can only pass two bytes. Thus, the timer will wrap at ~64 seconds. To prevent this, we scale the current time on the CPU side and unscale in the shader. This is still smaller than using `uniforms` or GLSL offsets.
- `native()` is just a shorthand to import *only* the WinAPI functions we need. Never use a standard include file. In fact, never include anything.
- `GLExt()` is a shorthand for accessing GLSL extensions.

## WinAPI Imports

Now define all needed types and import the WinAPI functions:

```cpp
extern "C" {
	native(int) wglGetProcAddress(const char*);
	native(int) wglMakeCurrent(int, int);
	struct BUTTER_PFDC { int a, b; };
	native(int) ChoosePixelFormat(int, BUTTER_PFDC*);
	native(int) SetPixelFormat(int, int, BUTTER_PFDC*);
	native(int) wglCreateContext(int);
	native(int) GetDC(int);
	native(int) GetAsyncKeyState(int);
	native(int) SetCursorPos(int, int);
	native(void) SwapBuffers(int);
	native(void) ExitProcess(int);
	native(void) glColor3us(unsigned short, unsigned short, unsigned short);
	native(void) glRects(short, short, short, short);
	native(int) GetTickCount();
	typedef void(__stdcall*glUseProgram)(int);
	typedef int(__stdcall*glCreateShaderProgramv)(int, int, const char**);
	static BUTTER_PFDC pfd = { 0, 37 };
}
```

`struct BUTTER_PFDC { int a, b; };` is a well-known alignment hack for the `PIXELFORMATDESCRIPTOR` struct. Turns out two `int`s is all you need.

## OpenGL context 

Next, declare the entry point:

```cpp
void entrypoint(void) {
  // we are now talking about this part
}
```

Now the second trick. Instead of creating a Window, we just render directly onto the desktop. Some GPUs do *not* support this:

```
const auto hDC = GetDC(0);
SetPixelFormat(hDC, ChoosePixelFormat(hDC, &pfd), &pfd);
```

Notice how the DC handle is declared as a `const`. That means the actual handle won't be stored as a variable, but rather remains in one of the 32bit registers *the whole time*. Compile the above code and inspect the assembly. You'll see that `hDC` will be stored in `EDI`. So now we know that `EDI` will always contain a positive, non-zero value which is also the DC. This will come in handy.

Using this knowledge, we can spare a few bytes on the actual OpenGL context initialization by re-using the register instead of creating a variable:

```cpp
_asm {
	push edi;
	call DWORD PTR wglCreateContext;
	push eax;
	push edi;
	call wglMakeCurrent;
}
```

## Shader

That's the OpenGL context done. Now time to write the shader. Use GLSLSandbox or any compatible GLSL editor to create your shader. Then just replace the `uniform float time` with `float time=gl_Color.r*192`, where `192` is the runtime in seconds (`RUNTIME` * 64). For now, let's stick to this minimal shader:

```cpp
static auto fragmentShader = "void main(){gl_FragColor=vec4(.5);}";
```

I.e. a grey solid. Make sure you declare the shader source as a `static` variable, so it'll be stored as data in the assembly. Compiling and selecting the shader is easy:

```cpp
GLExt(glUseProgram)(GLExt(glCreateShaderProgramv)(0x8B30, 1, &fragmentShader));
```

## Timing

That's the "creative" part done. Now on to the timing routine. First, get the current time after the shader init:

```cpp
const auto startTime = GetTickCount();
```

Oh look, another `const`! Where this is stored depends on the main loop code. For now, it is stored in `EBX`. Now, since we don't have a window, we can't use `ShowCursor`, because this function only works on the current window's DC. 

But we can *move* the cursor off-screen. We know that `EBX` holds a positive value (the current uptime in milliseconds), which is most likely to be bigger than any screen's vertical resolution. So we use that to clip the cursor to the bottom of the screen, where it is invisible:

```cpp
 _asm {
	push ebx;
	push 0;
	call SetCursorPos;
}
```

On a 1080p screen, there's only a 1.08 second window every 50 *days* where this won't work. Now the main loop:

```cpp
loop:
	auto elapsed = GetTickCount() - startTime;
	if (RUNTIME * 64000 < elapsed) goto panic;
	glColor3us(elapsed / RUNTIME, RUNTIME, RUNTIME);
	_asm {
		push edi;
		push edi;
		push - 1;
		push - 1;
		call glRects;
	}
	SwapBuffers(hDC);
	if(!GetAsyncKeyState(27)) goto loop;
```

Let's analyze what happens here:

- We use a simple goto label for the loop to prevent the compiler from doing something stupid.
- `elapsed` holds the elapsed time in ms. This isn't a real variable, it's just stored in `EAX` and will be re-used in the following statements.
- If the runtime elapsed, we exit. The exit is clean (more on that later). You can always make a graceful exit by jumping to `panic`. If you don't do this (e.g. by crashing the demo), the desktop DC will be fucked.
- We pass the time (scaled by the runtime) to the shader using the red color component of `glColor3us`. *Pay attention*  to the other colors. Even if we don't *use* the other colors, we pass the "same" value three times. This will result in three identical `push` instructions which will *always* compress at least one byte better than using other values (e.g. `0`).
- Remember `EDI`? Again, we make redundant `push` instructions by using `glRects(-1, -1, edi, edi)` to lower the entropy. The last two parameters are required to be greater than or equal to `1`. hDC is basically garantueed to be exactly that.

## Clean exit

You're thinking `ExitProcess(0)`, right? But `push 0` is not very common in our code. So what is? `push edi` ;-)

```cpp
panic:
	_asm {
		push edi;
		call ExitProcess;
	}
```

## Compile & Link

Compile:

```text
"C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\cl.exe" /c /O2 /Ob1 /Oi /Os /FAs demo.cpp
```

Every option here is essential, so don't fool around with this. Now link:

```text
link.exe /OUT:".\demo.exe" opengl32.lib glu32.lib winmm.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /SUBSYSTEM:WINDOWS /ENTRY:"entrypoint" /CRINKLER /HASHTRIES:4096 /COMPMODE:SLOW /ORDERTRIES:8192 /TINYHEADER /TINYIMPORT /UNALIGNCODE /REPORT:.\demo.html /SATURATE /UNSAFEIMPORT /LIBPATH:"C:\Program Files (x86)\Windows Kits\8.1\Lib\winv6.3\um\x86" /RANGE:opengl32 .\demo.obj
```

This will most likely produce the smallest code no matter what you do. In fact, for our solid grey shader above, the final size (runtime = 3) is 516 bytes:

```text
Linking...

Uncompressed size of code:   280
Uncompressed size of data:   115

|-- Estimating models -------------------------------------|
............................................................   0m01s

Estimated ideal compressed size: 281.64

Reordering sections...
  Iteration:     0  Size: 281.64
  Iteration:    15  Size: 280.84
Time spent: 0m08s

|-- Reestimating models -----------------------------------|
............................................................   0m00s

Reestimated ideal compressed size: 280.81

Output file: .\demo.exe
Final file size: 516 (no change)

time spent: 0m20s
```

## Advanced shaders

When writing a shader for this framework you want have two goals:

1. Make the GLSL code as short as possible. Read more about math (and use Wolfram Alpha to optimize formulas using `simplify[...]`). Don't rely on "minifiers".
2. At the same time, keep your code as redundant as possible.

Remember to 

- Replace `uniform float time;` with `float time=gl_Color.r*<RUNTIME*64>`.
- Replace `uniform vec2 resolution;` with a constant `vec2`.

## Example

The included `demo.cpp` includes this shader (472 bytes):

```c
float b=gl_Color.r*192,g,r,v,q;vec4 s(vec2 v){g=length(v);q=abs(sin((atan(v.g,v.r)-g+b)*9)*.1)+.1;return min(vec4(1),vec4(.05/abs(q-g/3),.04/abs(q-g/2),.03/abs(q-g*.7),1));}float n(vec3 v){return 1-dot(abs(v),vec3(0,1,0))-length(s(v.rb).rgb)/2*sin(b*2)+(sin(5*(v.b+b))+sin(5*(v.r+b)))*.1;}void main(){vec3 m=vec3(-1+2*(gl_FragCoord.rg/vec2(1366,768)),1),a=vec3(0,0,-2);for(;r<60;r++)g=n(a+m*v),v+=g*.125;gl_FragColor=vec4(v/2)*s((v*m+a).rb)+v*.1*vec4(1,2,3,4)/2*n(v*m+a);}
```

The final size for this (runtime = 3) with `vec2(1366,768)` as the resolution (my crappy screen) is 749 bytes:

```text
Linking...

Uncompressed size of code:   280
Uncompressed size of data:   552

Estimated ideal compressed size: 514.05

Reordering sections...
  Iteration:     0  Size: 514.06
  Iteration:    15  Size: 513.54
Time spent: 0m21s

Reestimated ideal compressed size: 513.53

Output file: .\demo.exe
Final file size: 749

time spent: 0m34s
```

## Demo

[![](https://raw.githubusercontent.com/hlecter/Kafka/master/video.png)](http://webm.land/media/9DGX.webm)

