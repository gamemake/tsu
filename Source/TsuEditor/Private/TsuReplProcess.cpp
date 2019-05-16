// Original work - Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.
// Modified work - Copyright 2019 Mikael Hermansson. All Rights Reserved.

#include "TsuReplProcess.h"

#include "TsuEditorLog.h"

#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"

static bool CreateOutputPipes(void*& ReadPipe, void*& WritePipe)
{
	SECURITY_ATTRIBUTES Security = {};
	Security.nLength = sizeof(SECURITY_ATTRIBUTES);
	Security.bInheritHandle = true;

	if (!::CreatePipe(&ReadPipe, &WritePipe, &Security, 0))
		return false;

	if (!::SetHandleInformation(ReadPipe, HANDLE_FLAG_INHERIT, 0))
		return false;

	return true;
}

static bool CreateInputPipes(void*& ReadPipe, void*& WritePipe)
{
	SECURITY_ATTRIBUTES Security = {};
	Security.nLength = sizeof(SECURITY_ATTRIBUTES);
	Security.bInheritHandle = true;

	if (!::CreatePipe(&ReadPipe, &WritePipe, &Security, 0))
		return false;

	if (!::SetHandleInformation(WritePipe, HANDLE_FLAG_INHERIT, 0))
		return false;

	return true;
}

static FProcHandle CreateProc(
	const TCHAR* URL,
	const TCHAR* Parms,
	void* StdOutWrite,
	void* StdInRead)
{
	SECURITY_ATTRIBUTES Security = {};
	Security.nLength = sizeof(SECURITY_ATTRIBUTES);
	Security.bInheritHandle = true;

	STARTUPINFO Startup = {};
	Startup.cb = sizeof(STARTUPINFO);
	Startup.dwX = CW_USEDEFAULT;
	Startup.dwY = CW_USEDEFAULT;
	Startup.dwXSize = CW_USEDEFAULT;
	Startup.dwYSize = CW_USEDEFAULT;
	Startup.dwFlags = (STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES);
	Startup.wShowWindow = SW_HIDE;
	Startup.hStdInput = (::HANDLE)StdInRead;
	Startup.hStdOutput = (::HANDLE)StdOutWrite;
	Startup.hStdError = (::HANDLE)StdOutWrite;

	FString CommandLine = FString::Printf(TEXT("\"%s\" %s"), URL, Parms);

	PROCESS_INFORMATION Process = {};

	if (!CreateProcess(
			NULL, // lpApplicationName
			&CommandLine[0], // lpCommandLine
			&Security, // lpProcessAttributes
			&Security, // lpThreadAttributes
			true, // bInheritHandles
			NORMAL_PRIORITY_CLASS, // dwCreationFlags
			NULL, // lpEnvironment
			nullptr, // lpCurrentDirectory
			&Startup, // lpStartupInfo
			&Process)) // lpProcessInformation
	{
		return FProcHandle();
	}

	::CloseHandle(Process.hThread);

	return FProcHandle(Process.hProcess);
}

#include "Windows/HideWindowsPlatformTypes.h"

#else // PLATFORM_WINDOWS

static bool CreateOutputPipes(void*& ReadPipe, void*& WritePipe)
{
	return FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
}

static bool CreateInputPipes(void*& ReadPipe, void*& WritePipe)
{
	return FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
}

static FProcHandle CreateProc(
	const TCHAR* URL,
	const TCHAR* Params,
	void* StdOutWrite,
	void* StdInRead)
{
	return FPlatformProcess::CreateProc(
		URL,
		Params,
		false,
		true,
		false,
		nullptr,
		0,
		nullptr,
		StdOutWrite,
		StdInRead
	);
}

#endif // PLATFORM_WINDOWS

FTsuReplProcess::FTsuReplProcess(
	FProcHandle InProcessHandle,
	void* InStdOutRead,
	void* InStdOutWrite,
	void* InStdInRead,
	void* InStdInWrite)
	: ProcessHandle(InProcessHandle)
	, StdOutRead(InStdOutRead)
	, StdOutWrite(InStdOutWrite)
	, StdInRead(InStdInRead)
	, StdInWrite(InStdInWrite)
{
}

FTsuReplProcess::FTsuReplProcess(FTsuReplProcess&& Other)
	: ProcessHandle(Other.ProcessHandle)
	, StdOutRead(Other.StdOutRead)
	, StdOutWrite(Other.StdOutWrite)
	, StdInRead(Other.StdInRead)
	, StdInWrite(Other.StdInWrite)
{
	Other.ProcessHandle.Reset();
}

FTsuReplProcess::~FTsuReplProcess()
{
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(StdOutRead, StdOutWrite);
		FPlatformProcess::ClosePipe(StdInRead, StdInWrite);

		FPlatformProcess::TerminateProc(ProcessHandle);
		FPlatformProcess::CloseProc(ProcessHandle);
	}
}

TOptional<FTsuReplProcess> FTsuReplProcess::Launch(
	const FString& ProcessPath,
	const FString& ProcessArgs)
{
	// #todo(#mihe): This function lacks proper cleanup on failure

	void* StdOutRead = nullptr;
	void* StdOutWrite = nullptr;
	if (!CreateOutputPipes(StdOutRead, StdOutWrite))
	{
		UE_LOG(LogTsuEditor, Error, TEXT("Failed to create output pipes"));
		return {};
	}

	void* StdInRead = nullptr;
	void* StdInWrite = nullptr;
	if (!CreateInputPipes(StdInRead, StdInWrite))
	{
		UE_LOG(LogTsuEditor, Error, TEXT("Failed to create input pipes"));
		return {};
	}

	FProcHandle ProcessHandle = CreateProc(
		*ProcessPath,
		*ProcessArgs,
		StdOutWrite,
		StdInRead);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogTsuEditor, Error, TEXT("Failed to create process"));
		return {};
	}

#if PLATFORM_WINDOWS
	static HANDLE JobHandle = []
	{
		HANDLE Result = CreateJobObject(NULL, NULL);
		check(Result != NULL);
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION LimitInformation = {};
		LimitInformation.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		verify(SetInformationJobObject(Result, JobObjectExtendedLimitInformation, &LimitInformation, sizeof(LimitInformation)));
		return Result;
	}();

	if (!AssignProcessToJobObject(JobHandle, ProcessHandle.Get()))
	{
		UE_LOG(LogTsuEditor, Error, TEXT("Failed to tie process to editor lifetime"));
		return {};
	}
#endif // PLATFORM_WINDOWS

	return FTsuReplProcess(
		ProcessHandle,
		StdOutRead,
		StdOutWrite,
		StdInRead,
		StdInWrite);
}

bool FTsuReplProcess::Write(const FString& Input)
{
	auto Cmd = Input + TEXT("\n");
	FTCHARToUTF8 Utf8(*Cmd);
	return FPlatformProcess::WritePipe(StdInWrite, (const uint8*)Utf8.Get(), (const uint32)Utf8.Length());
}

TOptional<FString> FTsuReplProcess::ReadOutput(double Timeout)
{
	ReadFromStdOut(Timeout);

	if (StdOut.Num() <= 0)
	{
		return {};
	}

	auto retval = StdOut[0];
	StdOut.RemoveAt(0);
	return retval;
}

static FString TsuResponseTag(TEXT("RESPONSE_TAG "));

void FTsuReplProcess::ReadFromStdOut(double Timeout)
{
	const double StartTime = FPlatformTime::Seconds();

	while (StdOut.Num() <= 0)
	{
		OutText += FPlatformProcess::ReadPipe(StdOutRead);

		for (;;)
		{
			auto idx = OutText.Find(TEXT("\n"));
			if (idx < 0) break;

			auto line = OutText.Mid(0, idx + 1);
			line.TrimStartInline();
			line.TrimEndInline();
			OutText = OutText.Mid(idx + 1);

			if (line.StartsWith(TsuResponseTag))
			{
				StdOut.Add(line.Mid(TsuResponseTag.Len()));
			}
			else if (line.Len() > 0)
			{
				UE_LOG(LogTsuEditor, Error, TEXT("%s"), *line);
			}
		}

		if (!FPlatformProcess::IsProcRunning(ProcessHandle))
			break;

		const double ElapsedTime = FPlatformTime::Seconds() - StartTime;
		if (ElapsedTime >= Timeout)
			break;
	}
}

bool FTsuReplProcess::IsRunning() const
{
	return FPlatformProcess::IsProcRunning(ProcessHandle);
}
