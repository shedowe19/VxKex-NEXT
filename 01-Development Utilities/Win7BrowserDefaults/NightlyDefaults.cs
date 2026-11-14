using System;
using System.IO;
using System.Text;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using System.Security.Principal;
using System.Xml;
using System.Collections.Generic;
using System.Text.RegularExpressions;

namespace VxKexNightlyDefaults {
  [ComImport, Guid("4e530b0a-e611-4c77-a3ac-9031d022281b"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
  interface IAssociationRegistration {
    [PreserveSig] int QueryCurrentDefault([MarshalAs(UnmanagedType.LPWStr)] string query, int type, int level, out IntPtr association);
    [PreserveSig] int QueryAppIsDefault([MarshalAs(UnmanagedType.LPWStr)] string query, int type, int level, [MarshalAs(UnmanagedType.LPWStr)] string app, [MarshalAs(UnmanagedType.Bool)] out bool result);
    [PreserveSig] int QueryAppIsDefaultAll(int level, [MarshalAs(UnmanagedType.LPWStr)] string app, [MarshalAs(UnmanagedType.Bool)] out bool result);
    [PreserveSig] int SetAppAsDefault([MarshalAs(UnmanagedType.LPWStr)] string app, [MarshalAs(UnmanagedType.LPWStr)] string association, int type);
    // The broader SetAppAsDefaultAll and ClearUserAssociations are intentionally not declared.
  }
  [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
  struct OsVersion {
    public uint Size, Major, Minor, Build, Platform;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst=128)] public string ServicePack;
  }
  public static class SetNightlyFour {
    const string App = "Brave Nightly";
    const string ProgId = "BraveSSHTM";
    static string Capabilities;
    static string Exe;
    static readonly string[] Associations = {"http", "https", ".htm", ".html", ".pdf", ".svg", "mailto"};
    static readonly int[] Types = {1, 1, 0, 0, 0, 0, 1};
    [DllImport("ntdll.dll")] static extern int RtlGetVersion(ref OsVersion version);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandleW(string module);
    [DllImport("shell32.dll")] static extern void SHChangeNotify(uint eventId, uint flags, IntPtr item1, IntPtr item2);

    static string Q(string value) {
      if(value==null)return "null";
      StringBuilder b=new StringBuilder("\"");
      foreach(char c in value) {
        if(c=='"'||c=='\\')b.Append('\\').Append(c);
        else if(c<32)b.Append("\\u").Append(((int)c).ToString("x4",CultureInfo.InvariantCulture));
        else b.Append(c);
      }
      return b.Append('"').ToString();
    }
    static string Hex(int value) {return "0x"+unchecked((uint)value).ToString("X8",CultureInfo.InvariantCulture);}
    static void NewFile(string path,string content) {
      using(FileStream f=new FileStream(path,FileMode.CreateNew,FileAccess.Write,FileShare.Read)) {
        byte[] bytes=new UTF8Encoding(false).GetBytes(content);f.Write(bytes,0,bytes.Length);f.Flush();
      }
    }
    static string CheckPath(string path,bool fresh,string extension) {
      if(String.IsNullOrEmpty(path)||path.Length<8||path[1]!=':'||path[2]!='\\'||!path.EndsWith(extension,StringComparison.OrdinalIgnoreCase))
        throw new ArgumentException("Use absolute local "+extension+" paths.");
      string full=Path.GetFullPath(path);
      if(!Directory.Exists(Path.GetDirectoryName(full)))throw new DirectoryNotFoundException("Report parent must already exist.");
      if(fresh&&File.Exists(full))throw new IOException("A fresh file path is required; no overwrite.");
      if(!fresh&&!File.Exists(full))throw new FileNotFoundException("The original backup was not found.");
      return full;
    }
    static string Value(RegistryKey root,string path,string name) {
      using(RegistryKey k=root.OpenSubKey(path,false)) {
        if(k==null)return null;
        object value=k.GetValue(name,null,RegistryValueOptions.DoNotExpandEnvironmentNames);
        return value==null?null:value.ToString();
      }
    }
    static void VerifyNativeUser() {
      WindowsIdentity identity=WindowsIdentity.GetCurrent();
      if(IntPtr.Size!=8||identity.IsSystem||identity.IsAnonymous||identity.User==null)
        throw new InvalidOperationException("Use the intended Windows user's native64 process, not a service account.");
      OsVersion v=new OsVersion();v.Size=(uint)Marshal.SizeOf(typeof(OsVersion));
      int status=RtlGetVersion(ref v);
      if(status<0||v.Major!=6||v.Minor!=1||GetModuleHandleW("KexDll.dll")!=IntPtr.Zero)
        throw new InvalidOperationException("Run natively on Windows7, outside VxKex.");
    }
    static void VerifyTarget() {
      VerifyNativeUser();
      Capabilities=Value(Registry.LocalMachine,@"Software\RegisteredApplications",App);
      if(String.IsNullOrEmpty(Capabilities)||!Regex.IsMatch(Capabilities,@"^Software\\Clients\\StartMenuInternet\\[^\\]+\\Capabilities$",RegexOptions.IgnoreCase))
        throw new InvalidOperationException("Machine Brave Nightly capabilities were not found in the supported registration layout.");
      if(Value(Registry.CurrentUser,@"Software\RegisteredApplications",App)!=null)
        throw new InvalidOperationException("A per-user Nightly registration requires separate review.");
      for(int i=0;i<4;i++) {
        string group=Types[i]==1?"URLAssociations":"FileAssociations";
        if(!String.Equals(Value(Registry.LocalMachine,Capabilities+"\\"+group,Associations[i]),ProgId,StringComparison.Ordinal))
          throw new InvalidOperationException("Nightly must register BraveSSHTM for exactly the four selected associations.");
      }
      string command=Value(Registry.LocalMachine,@"Software\Classes\BraveSSHTM\shell\open\command","");
      Match match=Regex.Match(command==null?"":command,"^\"([A-Za-z]:\\\\[^\"\\r\\n]+\\\\brave\\.exe)\"( --single-argument %1)$",RegexOptions.IgnoreCase);
      if(!match.Success||match.Groups[2].Value!=" --single-argument %1")throw new InvalidOperationException("Unsupported original Nightly command layout.");
      Exe=match.Groups[1].Value;
      if(!File.Exists(Exe)||FileVersionInfo.GetVersionInfo(Exe).ProductName.IndexOf("Nightly",StringComparison.OrdinalIgnoreCase)<0)
        throw new InvalidOperationException("The registered file is not identified as installed Nightly.");
      foreach(RegistryKey root in new RegistryKey[]{Registry.CurrentUser,Registry.LocalMachine})
        foreach(string path in new string[]{@"Software\Classes\BraveSSHTM\shell\open",@"Software\Classes\BraveSSHTM\shell\open\command"})
          using(RegistryKey key=root.OpenSubKey(path,false))if(key!=null)
            foreach(string name in key.GetValueNames())if(String.Equals(name,"DelegateExecute",StringComparison.OrdinalIgnoreCase))
              throw new InvalidOperationException("DelegateExecute requires separate review; no changes made.");
    }
    static string[,] Query(IAssociationRegistration api,out int[,] results) {
      string[,] values=new string[Associations.Length,3];results=new int[Associations.Length,3];
      for(int i=0;i<Associations.Length;i++)for(int level=0;level<3;level++) {
        IntPtr text=IntPtr.Zero;
        try {results[i,level]=api.QueryCurrentDefault(Associations[i],Types[i],level,out text);values[i,level]=text==IntPtr.Zero?null:Marshal.PtrToStringUni(text);}
        finally {if(text!=IntPtr.Zero)Marshal.FreeCoTaskMem(text);}
      }
      return values;
    }
    static string QueryJson(string[,] values,int[,] results) {
      StringBuilder b=new StringBuilder("[");
      for(int i=0;i<Associations.Length;i++)for(int level=0;level<3;level++) {
        if(i!=0||level!=0)b.Append(',');
        b.Append("{\"association\":").Append(Q(Associations[i])).Append(",\"type\":").Append(Types[i]).Append(",\"level\":").Append(level)
         .Append(",\"hresult\":").Append(Q(Hex(results[i,level]))).Append(",\"value\":").Append(Q(values[i,level])).Append('}');
      }
      return b.Append(']').ToString();
    }
    static string UserChoiceJson() {
      StringBuilder b=new StringBuilder("[");
      for(int i=0;i<4;i++) {
        if(i!=0)b.Append(',');
        string path=Types[i]==1?@"Software\Microsoft\Windows\Shell\Associations\UrlAssociations\"+Associations[i]+@"\UserChoice":
                                @"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\"+Associations[i]+@"\UserChoice";
        b.Append("{\"association\":").Append(Q(Associations[i])).Append(",\"path\":").Append(Q(path));
        using(RegistryKey key=Registry.CurrentUser.OpenSubKey(path,false)) {
          b.Append(",\"exists\":").Append(key==null?"false":"true").Append(",\"values\":[");
          if(key!=null) {
            string[] names=key.GetValueNames();Array.Sort(names,StringComparer.Ordinal);
            for(int j=0;j<names.Length;j++) {
              if(j!=0)b.Append(',');object data=key.GetValue(names[j],null,RegistryValueOptions.DoNotExpandEnvironmentNames);
              string serialized=data is byte[]?Convert.ToBase64String((byte[])data):data==null?null:data.ToString();
              b.Append("{\"name\":").Append(Q(names[j])).Append(",\"kind\":").Append(Q(key.GetValueKind(names[j]).ToString()))
               .Append(",\"data\":").Append(Q(serialized)).Append('}');
            }
          }
          b.Append("]}");
        }
      }
      return b.Append(']').ToString();
    }
    static string UserId() {return WindowsIdentity.GetCurrent().User.Value;}
    static bool AppMaps(string app,int index,string progId) {
      foreach(RegistryKey root in new RegistryKey[]{Registry.CurrentUser,Registry.LocalMachine}) {
        string cap=Value(root,@"Software\RegisteredApplications",app);
        if(cap!=null) {
          if(!cap.StartsWith(@"Software\",StringComparison.OrdinalIgnoreCase))return false;
          return String.Equals(Value(root,cap+"\\"+(Types[index]==1?"URLAssociations":"FileAssociations"),Associations[index]),progId,StringComparison.Ordinal);
        }
      }
      return false;
    }
    static string PreviousApp(IAssociationRegistration api,int index,string progId) {
      if(String.Equals(progId,ProgId,StringComparison.Ordinal)&&AppMaps(App,index,progId))return App;
      Dictionary<string,bool> seen=new Dictionary<string,bool>(StringComparer.OrdinalIgnoreCase);
      string answer=null;
      foreach(RegistryKey root in new RegistryKey[]{Registry.CurrentUser,Registry.LocalMachine})
        using(RegistryKey key=root.OpenSubKey(@"Software\RegisteredApplications",false)) {
          if(key==null)continue;
          foreach(string name in key.GetValueNames()) {
            if(seen.ContainsKey(name))continue;seen.Add(name,true);
            if(!AppMaps(name,index,progId))continue;
            bool isDefault;int hr=api.QueryAppIsDefault(Associations[index],Types[index],1,name,out isDefault);
            if(hr!=0||!isDefault)continue;
            if(answer!=null&&!String.Equals(answer,name,StringComparison.OrdinalIgnoreCase))return null;
            answer=name;
          }
        }
      return answer;
    }
    static string BackupXml(IAssociationRegistration api,string[,] before,int[,] hr) {
      XmlDocument document=new XmlDocument();
      XmlElement root=document.CreateElement("VxKexNightlyDefaults");document.AppendChild(root);
      root.SetAttribute("schema","1");root.SetAttribute("user",UserId());root.SetAttribute("app",App);
      root.SetAttribute("progId",ProgId);root.SetAttribute("exe",Exe);
      root.SetAttribute("createdUtc",DateTime.UtcNow.ToString("o",CultureInfo.InvariantCulture));
      for(int i=0;i<Associations.Length;i++) {
        XmlElement item=document.CreateElement("Association");root.AppendChild(item);
        item.SetAttribute("name",Associations[i]);item.SetAttribute("type",Types[i].ToString(CultureInfo.InvariantCulture));
        for(int level=0;level<3;level++) {
          XmlElement query=document.CreateElement("Query");item.AppendChild(query);
          query.SetAttribute("level",level.ToString(CultureInfo.InvariantCulture));query.SetAttribute("hr",Hex(hr[i,level]));
          if(before[i,level]!=null)query.SetAttribute("value",before[i,level]);
        }
        if(i<4) {
          if(hr[i,1]!=0||String.IsNullOrEmpty(before[i,1]))throw new InvalidOperationException("A selected effective default could not be backed up.");
          string oldApp=PreviousApp(api,i,before[i,1]);
          if(String.IsNullOrEmpty(oldApp))throw new InvalidOperationException("The previous effective handler has no unique registered application for supported COM restoration.");
          item.SetAttribute("previousApp",oldApp);
        }
      }
      XmlElement raw=document.CreateElement("RawUserChoiceAuditOnly");raw.InnerText=UserChoiceJson();root.AppendChild(raw);
      return document.OuterXml+"\n";
    }
    static void ReadRestore(string path,out string[] oldProgIds,out string[] oldApps) {
      XmlReaderSettings settings=new XmlReaderSettings();
      // ProhibitDtd is the CLR2 API available on Windows7's original PowerShell2.
#pragma warning disable 618
      settings.ProhibitDtd=true;
#pragma warning restore 618
      settings.XmlResolver=null;
      XmlDocument document=new XmlDocument();document.XmlResolver=null;
      using(XmlReader reader=XmlReader.Create(path,settings))document.Load(reader);
      XmlElement root=document.DocumentElement;
      if(root==null||root.Name!="VxKexNightlyDefaults"||root.GetAttribute("schema")!="1"||root.GetAttribute("user")!=UserId()||
         root.GetAttribute("app")!=App||root.GetAttribute("progId")!=ProgId||!Regex.IsMatch(root.GetAttribute("exe"),@"^[A-Za-z]:\\[^""\r\n]+\\brave\.exe$",RegexOptions.IgnoreCase))
        throw new InvalidOperationException("Backup identity/target mismatch.");
      XmlNodeList items=root.SelectNodes("Association");
      if(items.Count!=Associations.Length)throw new InvalidOperationException("Backup association count mismatch.");
      oldProgIds=new string[4];oldApps=new string[4];
      for(int i=0;i<Associations.Length;i++) {
        XmlElement item=(XmlElement)items[i];
        if(item.GetAttribute("name")!=Associations[i]||item.GetAttribute("type")!=Types[i].ToString(CultureInfo.InvariantCulture))
          throw new InvalidOperationException("Backup association scope mismatch.");
        if(i<4) {
          XmlElement query=(XmlElement)item.SelectSingleNode("Query[@level='1']");
          if(query==null||query.GetAttribute("hr")!="0x00000000")throw new InvalidOperationException("Backup effective query failed.");
          oldProgIds[i]=query.GetAttribute("value");oldApps[i]=item.GetAttribute("previousApp");
          if(String.IsNullOrEmpty(oldProgIds[i])||String.IsNullOrEmpty(oldApps[i]))
            throw new InvalidOperationException("A previous registered application/handler was not backed up.");
        }
      }
    }
    static void Event(StreamWriter journal,string json) {journal.WriteLine(json);journal.Flush();Console.Out.WriteLine(json);}
    public static int Run(string backupPath,string resultPath,string mode) {
      object instance=null;
      try {
        if(mode!="Plan"&&mode!="Apply"&&mode!="Restore")throw new ArgumentException("Use Plan, Apply, or Restore.");
        bool restore=mode=="Restore",apply=mode!="Plan";
        string backup=CheckPath(backupPath,!restore,".xml"),result=CheckPath(resultPath,true,".jsonl");
        if(String.Equals(backup,result,StringComparison.OrdinalIgnoreCase))throw new ArgumentException("Report paths must differ.");
        VerifyNativeUser();if(!restore)VerifyTarget();
        instance=Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("591209c7-767b-42b2-9fba-44ee4615f2c7")));
        IAssociationRegistration api=(IAssociationRegistration)instance;
        int[,] beforeHr;string[,] before=Query(api,out beforeHr);
        string[] destinations=new string[4],apps=new string[4];bool[] forceSet=new bool[4];
        if(restore) {
          ReadRestore(backup,out destinations,out apps);
          for(int i=0;i<4;i++) {
            bool isOld=beforeHr[i,1]==0&&String.Equals(before[i,1],destinations[i],StringComparison.Ordinal);
            string choice=Value(Registry.CurrentUser,Types[i]==1?@"Software\Microsoft\Windows\Shell\Associations\UrlAssociations\"+Associations[i]+@"\UserChoice":
              @"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\"+Associations[i]+@"\UserChoice","Progid");
            bool isOwned=beforeHr[i,1]==0?String.Equals(before[i,1],ProgId,StringComparison.Ordinal):String.Equals(choice,ProgId,StringComparison.Ordinal);
            if(!isOld&&!isOwned)
              throw new InvalidOperationException("A selected default changed after Apply; restoration refused before any changes.");
            forceSet[i]=String.Equals(choice,ProgId,StringComparison.Ordinal)&&!String.Equals(destinations[i],ProgId,StringComparison.Ordinal);
            if((!isOld||forceSet[i])&&!AppMaps(apps[i],i,destinations[i]))throw new InvalidOperationException("A previous registered application is missing or no longer maps the saved handler.");
          }
        } else {
          NewFile(backup,BackupXml(api,before,beforeHr));
          for(int i=0;i<4;i++){destinations[i]=ProgId;apps[i]=App;}
        }
        int failure=0,succeeded=0;
        using(FileStream file=new FileStream(result,FileMode.CreateNew,FileAccess.Write,FileShare.Read))using(StreamWriter journal=new StreamWriter(file,new UTF8Encoding(false))) {
          Event(journal,"{\"event\":\"begin\",\"mode\":"+Q(mode)+",\"backup\":"+Q(backup)+",\"method\":\"SetAppAsDefault only\"}");
          if(apply)for(int i=0;i<4;i++) {
            if(!forceSet[i]&&beforeHr[i,1]==0&&String.Equals(before[i,1],destinations[i],StringComparison.Ordinal)) {
              Event(journal,"{\"event\":\"already-default\",\"association\":"+Q(Associations[i])+"}");continue;
            }
            Event(journal,"{\"event\":\"before-set\",\"association\":"+Q(Associations[i])+",\"registeredApp\":"+Q(apps[i])+"}");
            int hr=api.SetAppAsDefault(apps[i],Associations[i],Types[i]);
            Event(journal,"{\"event\":\"set-returned\",\"association\":"+Q(Associations[i])+",\"hresult\":"+Q(Hex(hr))+"}");
            if(hr!=0){failure=10;break;}succeeded++;
          }
          if(succeeded>0)SHChangeNotify(0x08000000,0,IntPtr.Zero,IntPtr.Zero);
          int[,] afterHr;string[,] after=Query(api,out afterHr);
          bool targetGood=true,controlsUnchanged=true;
          for(int i=0;i<4;i++)if(afterHr[i,1]!=0||!String.Equals(after[i,1],destinations[i],StringComparison.Ordinal))targetGood=false;
          for(int i=4;i<Associations.Length;i++)for(int level=0;level<3;level++)
            if(beforeHr[i,level]!=afterHr[i,level]||!String.Equals(before[i,level],after[i,level],StringComparison.Ordinal))controlsUnchanged=false;
          Event(journal,"{\"event\":\"after-query\",\"values\":"+QueryJson(after,afterHr)+",\"fourEffectiveTargetsMatch\":"+(targetGood?"true":"false")+
                        ",\"pdfSvgMailtoControlsUnchanged\":"+(controlsUnchanged?"true":"false")+",\"rawUserChoice\":"+UserChoiceJson()+"}");
          if(apply&&failure==0&&!targetGood)failure=11;if(!controlsUnchanged)failure=12;
          Event(journal,"{\"event\":\"complete\",\"exitCode\":"+failure+",\"setsSucceeded\":"+succeeded+"}");
        }
        return failure;
      } catch(Exception ex) {Console.Error.WriteLine(ex.GetType().FullName+": "+ex.Message);return 2;}
      finally {if(instance!=null&&Marshal.IsComObject(instance))Marshal.ReleaseComObject(instance);}
    }
  }
}
