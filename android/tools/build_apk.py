"""Build a signed offline Android APK with the installed Android SDK/NDK and a JDK.
No Gradle downloads are needed. Private signing files never go into deliverables.
"""
from pathlib import Path
import argparse, json, os, secrets, shutil, subprocess, zipfile, uuid

SOURCE_ROOT = Path(__file__).resolve().parents[2]
# Android's aapt2 on Windows cannot reliably open Chinese paths. Build an ASCII snapshot.
ROOT = Path(os.environ.get('FORGE_ANDROID_BUILD_DIR', Path.home()/'Documents/Codex/work/sword-forge-android-build'))
ROOT.mkdir(parents=True,exist_ok=True)
for folder in ('src','include','web'):
    shutil.copytree(SOURCE_ROOT/folder,ROOT/folder,dirs_exist_ok=True)
shutil.copytree(SOURCE_ROOT/'android/app/src',ROOT/'android/app/src',dirs_exist_ok=True)
ANDROID = ROOT / 'android'
BUILD = ROOT / 'build' / uuid.uuid4().hex
SDK = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home()/'AppData/Local/Android/Sdk'))
JDK = Path(os.environ.get('FORGE_JAVA_HOME', 'C:/Program Files/Android/Android Studio1/jbr'))
parser=argparse.ArgumentParser();parser.add_argument('--debug',action='store_true');args=parser.parse_args()
PLATFORM=SDK/'platforms/android-35/android.jar'
TOOLS=SDK/'build-tools/36.1.0'
NDK=SDK/'ndk/26.1.10909125/toolchains/llvm/prebuilt/windows-x86_64'
for required in (JDK/'bin/javac.exe',TOOLS/'aapt2.exe',PLATFORM,NDK/'bin/clang.exe'):
    if not required.is_file(): raise SystemExit(f'Missing build tool: {required}')
BUILD.mkdir(parents=True,exist_ok=True)

def run(command,env=None):
    result=subprocess.run([str(c) for c in command],cwd=ROOT,env=env,check=False)
    if result.returncode: raise SystemExit(f'Build step failed ({result.returncode}): {Path(str(command[0])).name}')

# Strip a possible Windows UTF-8 BOM before passing source to javac.
source=ANDROID/'app/src/main/java/cn/swordforge/game/MainActivity.java'
source.write_text(source.read_text(encoding='utf-8-sig'),encoding='utf-8')
manifest=BUILD/'AndroidManifest.xml'
m=(ANDROID/'app/src/main/AndroidManifest.xml').read_text(encoding='utf-8-sig')
if args.debug:m=m.replace('<application ', '<application android:debuggable="true" ',1)
manifest.write_text(m,encoding='utf-8')
assets=BUILD/'assets/web';assets.mkdir(parents=True,exist_ok=True)
for name in ('index.html','styles.css','app.js', 'scene.js','dungeon.js','vendor/rot.js','vendor/ROT-LICENSE.txt','assets/forge.svg'):
    target=assets/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/'web'/name,target)
run([TOOLS/'aapt2.exe','compile','--dir',ANDROID/'app/src/main/res','-o',BUILD/'resources.zip'])
run([TOOLS/'aapt2.exe','link','-I',PLATFORM,'--manifest',manifest,'-A',BUILD/'assets','-o',BUILD/'base.apk',BUILD/'resources.zip'])
classes=BUILD/'classes';classes.mkdir(exist_ok=True)
run([JDK/'bin/javac.exe','-encoding','UTF-8','--release','8','-classpath',PLATFORM,'-d',classes,source])
with zipfile.ZipFile(BUILD/'classes.jar','w',zipfile.ZIP_DEFLATED) as z:
    for f in classes.rglob('*.class'):z.write(f,f.relative_to(classes).as_posix())
dex=BUILD/'dex';dex.mkdir(exist_ok=True)
run([JDK/'bin/java.exe','-cp',TOOLS/'lib/d8.jar','com.android.tools.r8.D8','--release','--min-api','26','--lib',PLATFORM,'--output',dex,BUILD/'classes.jar'])

sources=[ROOT/'src'/n for n in ('data.c','game.c','forge.c','adventure.c', 'delve.c','save.c','game_api.c')]
sources.append(ANDROID/'app/src/main/cpp/android_bridge.c')
abis={'arm64-v8a':'aarch64-linux-android26','armeabi-v7a':'armv7a-linux-androideabi26','x86_64':'x86_64-linux-android26'}
for abi,target in abis.items():
    output=BUILD/'lib'/abi/'libswordforge.so';output.parent.mkdir(parents=True,exist_ok=True)
    run([NDK/'bin/clang.exe',f'--target={target}',f'--sysroot={NDK}/sysroot','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-Wformat=2','-O2','-fPIC','-shared','-Wl,-z,max-page-size=16384','-Wl,-z,common-page-size=16384','-Wl,--no-undefined','-Wl,-soname,libswordforge.so','-I',ROOT/'include',*sources,'-o',output])
    run([NDK/'bin/llvm-strip.exe','--strip-unneeded',output])
    print(f'Compiled native C engine: {abi}',flush=True)
unsigned=BUILD/'unsigned.apk'
with zipfile.ZipFile(BUILD/'base.apk','r') as base, zipfile.ZipFile(unsigned,'w',zipfile.ZIP_DEFLATED) as z:
    for entry in base.infolist(): z.writestr(entry,base.read(entry.filename))
    z.write(dex/'classes.dex','classes.dex')
    for abi in abis:z.write(BUILD/'lib'/abi/'libswordforge.so',f'lib/{abi}/libswordforge.so')
run([TOOLS/'zipalign.exe','-f','-p','4',unsigned,BUILD/'aligned.apk'])
private=SOURCE_ROOT/'android/.private';private.mkdir(exist_ok=True)
credentials=private/'signing.json'
if not credentials.exists():credentials.write_text(json.dumps({'password':secrets.token_urlsafe(32)}),encoding='utf-8')
password=json.loads(credentials.read_text(encoding='utf-8'))['password']
env=dict(os.environ);env['FORGE_KEYSTORE_PASSWORD']=password
keystore=private/'swordforge.keystore'
if not keystore.exists():
    run([JDK/'bin/keytool.exe','-genkeypair','-keystore',keystore,'-alias','swordforge','-keyalg','RSA','-keysize','2048','-validity','10000','-storepass:env','FORGE_KEYSTORE_PASSWORD','-keypass:env','FORGE_KEYSTORE_PASSWORD','-dname','CN=Sword Forge Local,OU=Game,O=SwordForge,C=CN'],env=env)
output=SOURCE_ROOT/'outputs'/('SwordForge-android-test.apk' if args.debug else '剑炉物语-安卓版.apk')
output.parent.mkdir(exist_ok=True)
run([JDK/'bin/java.exe','-jar',TOOLS/'lib/apksigner.jar','sign','--ks',keystore,'--ks-key-alias','swordforge','--ks-pass','env:FORGE_KEYSTORE_PASSWORD','--key-pass','env:FORGE_KEYSTORE_PASSWORD','--out',output,BUILD/'aligned.apk'],env=env)
run([JDK/'bin/java.exe','-jar',TOOLS/'lib/apksigner.jar','verify','--verbose',output])
run([TOOLS/'zipalign.exe','-c','4',output])
print(f'APK ready: {output} ({output.stat().st_size} bytes)',flush=True)
