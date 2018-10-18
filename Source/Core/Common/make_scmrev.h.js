var wshShell		= new ActiveXObject("WScript.Shell")
var oFS				= new ActiveXObject("Scripting.FileSystemObject");

var outfile			= "./scmrev.h";
var cmd_commit	    = " rev-parse HEAD";
var cmd_version		= " describe --tags --always";

function GetGitExe()
{
	try
	{
		gitexe = wshShell.RegRead("HKCU\\Software\\GitExtensions\\gitcommand");
		wshShell.Exec(gitexe);
		return gitexe;
	}
	catch (e)
	{}

	for (var gitexe in {"git.cmd":1, "git":1, "git.bat":1})
	{
		try
		{
			wshShell.Exec(gitexe);
			return gitexe;
		}
		catch (e)
		{}
	}

	// last try - msysgit not in path (vs2015 default)
	msyspath = "\\Git\\cmd\\git.exe";
	gitexe = wshShell.ExpandEnvironmentStrings("%PROGRAMFILES(x86)%") + msyspath;
	if (oFS.FileExists(gitexe)) {
		return gitexe;
	}
	gitexe = wshShell.ExpandEnvironmentStrings("%PROGRAMFILES%") + msyspath;
	if (oFS.FileExists(gitexe)) {
		return gitexe;
	}

	WScript.Echo("Cannot find git or git.cmd, check your PATH:\n" +
		wshShell.ExpandEnvironmentStrings("%PATH%"));
	WScript.Quit(1);
}

function GetFirstStdOutLine(cmd)
{
	try
	{
		return wshShell.Exec(cmd).StdOut.ReadLine();
	}
	catch (e)
	{
		// catch "the system cannot find the file specified" error
		WScript.Echo("Failed to exec " + cmd + " this should never happen");
		WScript.Quit(1);
	}
}

function GetFileContents(f)
{
	try
	{
		return oFS.OpenTextFile(f).ReadAll();
	}
	catch (e)
	{
		// file doesn't exist
		return "";
	}
}

// get info from git
var gitexe = GetGitExe();
var commit = GetFirstStdOutLine(gitexe + cmd_commit);
var version	= GetFirstStdOutLine(gitexe + cmd_version);

var out_contents =
	"#define GIT_COMMIT \"" + commit + "\"\n" +
	"#define GIT_VERSION \"" + version + "\"\n";

// check if file needs updating
if (out_contents == GetFileContents(outfile))
{
	WScript.Echo(outfile + " current at " + revision);
}
else
{
	// needs updating - writeout current info
	oFS.CreateTextFile(outfile, true).Write(out_contents);
	WScript.Echo(outfile + " updated to " + revision);
}
