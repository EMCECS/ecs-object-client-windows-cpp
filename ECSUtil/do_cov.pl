sub DeleteGlob;


$currentdirsave = `cd`;
chop($currentdirsave);
chdir("..");
$SRC_DIR = `cd`;
chop($SRC_DIR);
chdir($currentdirsave);
print("SRC = $SRC_DIR\n");
print("Cur = $currentdirsave\n");
## delete the certificates so they can be regenerated
$CERT_PATH = $ENV{"APPDATA"};
print("cert = $CERT_PATH\\Coverity\\certs\\tft\\\n");
DeleteGlob("$CERT_PATH\\Coverity\\certs\\tft\\", "*.pem");

system("cov-build", "--dir", "..\\cov-int", "msbuild.exe", "..\\ECSUtil.sln", "/t:ECSUtil:Rebuild", "/p:PreferredToolArchitecture=x64", "/p:Configuration=Release", "/p:Platform=x64", "/verbosity:minimal", "/maxcpucount", "/fileLoggerParameters:Append;verbosity=Detailed");
if ($? != 0)
{
	print("*** Errors encountered in cov-build\n");
	exit(1);
}

system("cov-analyze", "--dir", "..\\cov-int", "--strip-path", $SRC_DIR);
if ($? != 0)
{
	print("*** Errors encountered in cov-analyze\n");
	exit(1);
}

$AUTH_KEY_PATH = `echo %USERPROFILE%\\auth-key.txt`;
chop($AUTH_KEY_PATH);
system("cov-commit-defects", "--url", "https://isg-coverity3.cec.lab.emc.com",
	"--stream", "feature-evstreamer", "--dir", "..\\cov-int", "--auth-key-file", $AUTH_KEY_PATH,
	"--certs", "Dell_Technologies_Issuing_CA_101.pem");
if ($? != 0)
{
	print("*** Errors encountered in cov-analyze\n");
	exit(1);
}


sub DeleteGlob
{
	my($DESTDIR, $STAR) = @_;
	my($CURDIR);
	$CURDIR = `cd`;
	chop($CURDIR);
	chdir($DESTDIR);
	unlink glob $STAR;
	chdir($CURDIR);
}
