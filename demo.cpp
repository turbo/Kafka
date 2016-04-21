#define RUNTIME 3
#define native(t) __declspec(dllimport) t __stdcall
#define GLExt(a) a(wglGetProcAddress(#a))

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

void entrypoint(void) {
	const auto hDC = GetDC(0);
	SetPixelFormat(hDC, ChoosePixelFormat(hDC, &pfd), &pfd);

	_asm {
		push edi;
		call DWORD PTR wglCreateContext;
		push eax;
		push edi;
		call wglMakeCurrent;
	}

	static auto fragmentShader = "float b=gl_Color.r*192,g,r,v,q;vec4 s(vec2 v){g=length(v);q=abs(sin((atan(v.g,v.r)-g+b)*9)*.1)+.1;return min(vec4(1),vec4(.05/abs(q-g/3),.04/abs(q-g/2),.03/abs(q-g*.7),1));}float n(vec3 v){return 1-dot(abs(v),vec3(0,1,0))-length(s(v.rb).rgb)/2*sin(b*2)+(sin(5*(v.b+b))+sin(5*(v.r+b)))*.1;}void main(){vec3 m=vec3(-1+2*(gl_FragCoord.rg/vec2(1366,768)),1),a=vec3(0,0,-2);for(;r<60;r++)g=n(a+m*v),v+=g*.125;gl_FragColor=vec4(v/2)*s((v*m+a).rb)+v*.1*vec4(1,2,3,4)/2*n(v*m+a);}";
	//~ static auto fragmentShader = "void main(){gl_FragColor=vec4(.5);}";

	GLExt(glUseProgram)(GLExt(glCreateShaderProgramv)(0x8B30, 1, &fragmentShader));

	const auto startTime = GetTickCount();

	_asm {
		push ebx;
		push 0;
		call SetCursorPos;
	}

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
panic:
	_asm {
		push edi;
		call ExitProcess;
	}
}