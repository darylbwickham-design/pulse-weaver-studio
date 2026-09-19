using PulseWeaver.Setup;
using System.IO.Compression;
using System.Text.Json;

var temp=Path.Combine(Path.GetTempPath(),"PulseWeaver-recovery-tests-"+Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(temp);
void Check(bool ok,string message){if(!ok)throw new Exception(message);}
void Put(string root,string path,string text){var dest=Path.Combine(root,path);Directory.CreateDirectory(Path.GetDirectoryName(dest)!);File.WriteAllText(dest,text);}
void Seed(string root,string version){Put(root,"bin/64bit/PulseWeaverCore.exe","synthetic "+version);Put(root,"bin/64bit/pulseweaver-update.json",JsonSerializer.Serialize(new {tag="v"+version}));Put(root,"config/scenes.json","scene UUIDs, transforms, "+version);Put(root,"extra-plugin.dll","keep plugin");}
try{
 var root=Path.Combine(temp,"Studio");Seed(root,"1.12.7");
 using(var held=Recovery.Acquire(root)){
   var blocked=false;try{using var second=Recovery.Acquire(root);}catch(IOException){blocked=true;}Check(blocked,"concurrent maintenance allowed");
 }
 var backup=Recovery.Backup(root);Check(Recovery.Inspect(backup).Version=="1.12.7","wrong backup version");
 var newer=Path.Combine(temp,"new");Seed(newer,"1.12.8");Put(newer,"new-only.dll","new");
 Recovery.Replace(root,newer);Check(Recovery.InstalledVersion(root)=="1.12.8","update failed");
 Recovery.Restore(root,backup,(_,_)=>{});Check(Recovery.InstalledVersion(root)=="1.12.7","rollback version failed");Check(File.ReadAllText(Path.Combine(root,"config/scenes.json")).EndsWith("1.12.7"),"settings not restored");Check(!File.Exists(Path.Combine(root,"new-only.dll")),"rollback left new files");
 Check(Directory.GetFiles(Recovery.BackupDirectory(root),"*.zip").Length==2,"restore must first back up current state");
 // Every move boundary rolls back on ordinary failures.
 for(int stop=1;stop<=8;stop++){
   var instance=Path.Combine(temp,"fault"+stop);Seed(instance,"1.12.7");var stage=Path.Combine(temp,"stage"+stop);Seed(stage,"1.12.8");Put(stage,"new.dll","new");
   bool failed=false;try{Recovery.Replace(instance,stage,step=>{if(step==stop)throw new IOException("Injected failure");});}catch(IOException){failed=true;}
   if(failed){Check(Recovery.InstalledVersion(instance)=="1.12.7","fault did not restore runtime");Check(File.ReadAllText(Path.Combine(instance,"config/scenes.json")).EndsWith("1.12.7"),"fault lost settings");Check(!Recovery.Pending(instance),"ordinary failure left pending journal");}
 }
 // Simulate termination after old bin moved aside and new bin installed.
 var interrupted=Path.Combine(temp,"interrupted");Seed(interrupted,"1.12.7");var work=Recovery.WorkDirectory(interrupted);Directory.CreateDirectory(Path.Combine(work,"previous"));
 var names=Directory.GetFileSystemEntries(interrupted).Select(Path.GetFileName).ToArray();
 File.WriteAllText(Path.Combine(work,"transaction.json"),JsonSerializer.Serialize(new {Schema=1,Root=interrupted,Before=names,After=names,Committed=false}));
 Directory.Move(Path.Combine(interrupted,"bin"),Path.Combine(work,"previous","bin"));Put(interrupted,"bin/64bit/pulseweaver-update.json","{\"tag\":\"v1.12.8\"}");
 Recovery.RecoverInterrupted(interrupted);Check(Recovery.InstalledVersion(interrupted)=="1.12.7","interrupted swap recovery failed");
 foreach(var path in new[]{"../escape","C:/escape","/absolute","config/../escape","file:ads","bin/NUL.txt","dir./file"}){bool rejected=false;try{Recovery.SafePath(root,path);}catch(IOException){rejected=true;}Check(rejected,"unsafe path accepted: "+path);}
 var corrupt=Path.Combine(temp,"corrupt.zip");File.Copy(backup,corrupt);using(var zip=ZipFile.Open(corrupt,ZipArchiveMode.Update)){var item=zip.GetEntry("files/config/scenes.json")!;item.Delete();using var writer=new StreamWriter(zip.CreateEntry("files/config/scenes.json").Open());writer.Write("corrupt");}
 bool bad=false;try{Recovery.Restore(root,corrupt,(_,_)=>{});}catch(IOException){bad=true;}Check(bad,"corruption accepted");Check(Recovery.InstalledVersion(root)=="1.12.7","invalid backup changed installation");
 Console.WriteLine("PASS: full backup, verified restore, rollback, config identity, obsolete files, concurrency, failure injection, interrupted recovery, unsafe paths and corruption rejection.");
 return 0;
}catch(Exception ex){Console.Error.WriteLine(ex);return 1;}
finally{Directory.Delete(temp,true);}
