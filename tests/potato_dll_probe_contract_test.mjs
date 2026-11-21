import assert from 'node:assert/strict';
import fs from 'node:fs';

const read = path => fs.readFileSync(new URL(`../${path}`, import.meta.url), 'utf8');
const ci = read('.github/workflows/ci.yml');
const workflow = read('.github/workflows/potato-dll-probe.yml');
const prepare = read('tests/potato-dll/prepare.py');
const compare = read('tests/potato-dll/compare_references.py');
const run = read('tests/potato-dll/run-offline.ps1');
const host = read('tests/potato-dll/probe.cpp');
const cmake = read('tests/potato-dll/CMakeLists.txt');

assert.match(ci, /test_potato_dll:[\s\S]*?type: boolean[\s\S]*?default: false/);
assert.match(ci, /github\.event_name == 'workflow_dispatch' && inputs\.test_potato_dll && !inputs\.test_supplied_dll/);
assert.match(ci, /uses: \.\/\.github\/workflows\/potato-dll-probe\.yml\s+with:\s+execute_original_dll: \$\{\{ inputs\.test_potato_dll \}\}/);
assert.equal((ci.match(/!inputs\.test_supplied_dll && !inputs\.test_potato_dll/g) || []).length, 4,
  'An explicit payload probe must not also queue ordinary application builds');
assert.match(ci, /node tests\/potato_dll_probe_contract_test\.mjs/);
assert.match(ci, /- name: Test Potato reference identities with synthetic data only \(no supplied DLL execution\)\s+run: python -m unittest discover -s tests\/potato-dll -p '\*_test\.py'/);
assert.match(compare, /ed56050e890d9bf9efa8df08cd5eee2f143041cb49b983760c231e2c1fbca449/);
assert.match(compare, /old\["data"\] == row\["data"\]/);
assert.match(compare, /sameDllMultiplicity/);
assert.doesNotMatch(compare, /\b(?:ctypes|WinDLL|CDLL|LoadLibrary|urllib|requests|socket|subprocess)\b/,
  'Reference comparison must not load supplied code, acquire network data or launch an execution harness');
assert.doesNotMatch(compare, /archive\.extract(?:all)?\s*\(/);
assert.match(workflow, /on:\s*\n  workflow_call:/);
assert.doesNotMatch(workflow, /^\s*(pull_request|pull_request_target|push|schedule|workflow_dispatch):/m);
assert.match(workflow, /github\.event_name == 'workflow_dispatch' && inputs\.execute_original_dll/);
assert.match(workflow, /runs-on: windows-2022/);
assert.match(workflow, /persist-credentials: false/);
assert.match(workflow, /permissions:\s+contents: read/);
assert.doesNotMatch(workflow, /secrets:|GH_TOKEN:|GITHUB_TOKEN:|self-hosted/);
assert.match(workflow, /-ExplicitlyAllowOriginalDll/);
assert.match(workflow, /potato-probe-results\/\*\.json/);
assert.doesNotMatch(workflow, /path:.*\.(dll|exe|rar|7z|zip|log)/i);
assert.match(prepare, /80751bed1151e7f5bbcf186b51b8aada3b446af0278e5575384de7fa9eeb8b17/);
assert.match(prepare, /19c74aad7b09e682473b8de4c35b47e6498de97b71cae5511a231b8eb02f2b22/);
for (const hash of [
  '25da83f0c7f5d6052e67e279cb427d20643593321405f017331ebd450e5fdcf8',
  'c956f2bd04b22212ecb8e65a0397c40addf64be136d8d1cd906e473661113554',
  '289e1eb34e815b886f4c2585c37efc78c489f58fd7b28942cfd5aeb1ec38b282',
  '7440db72c09af59e8a3249e17fff0a1e964e4409bc6ec7b9c1686b35368fac22',
  'b68e5b3586ffd2a57c680a0426818e71b06eadc3ce60b80507f42679892b3558'
]) assert.ok(prepare.includes(hash));
assert.match(prepare, /sha\(data\) == expected/);
assert.match(prepare, /sha\(path\.read_bytes\(\)\) == ARCHIVE_SHA256/);
assert.match(prepare, /len\(infos\) == 17/);
assert.match(prepare, /not info\.is_symlink\(\)/);
assert.match(prepare, /"\.bin"/);
assert.doesNotMatch(prepare, /\b(ctypes|WinDLL|CDLL|extractall)\b/);
assert.match(run, /if \(-not \$ExplicitlyAllowOriginalDll\)/);
assert.match(run, /-Direction Outbound -Program \$programs\[\$kind\] -Action Block -Profile Any -Enabled True/);
assert.ok(run.indexOf('Per-image outbound block verification failed') < run.indexOf('Copy-Item $source $dll'));
assert.ok(run.indexOf('Per-image outbound block verification failed') < run.indexOf('& $launcher --launch'));
assert.match(run, /Firewall unavailable; original DLL NOT run/);
assert.match(run, /@\('texture','sampler'\)/);
assert.match(run, /\$rows.Count -eq 12/);
assert.match(run, /gameAcceptance=\$false/);
assert.match(run, /childExitCode/);
assert.match(run, /mechanismObserved/);
assert.match(run, /swapEntryLeadsIntoOriginal/);
assert.match(run, /Write-BoundedEvidence -Title 'Potato probe summary'/);
assert.match(run, /Write-BoundedEvidence -Title 'Independent owned sampler cases'/);
assert.match(run, /\$compact.Length -gt 3800/);
assert.match(run, /\$i \+= 2/);
assert.match(host, /LOAD\(void,BindTexture,GLenum,GLuint\)/);
assert.match(host, /glBindTexture\(GL_TEXTURE_2D,exportOnly\)/);
assert.match(host, /bindExportEntryLeadsIntoOriginal/);
assert.match(host, /bindExportOnlyBias/);
assert.match(host, /CREATE_SUSPENDED\|CREATE_UNICODE_ENVIRONMENT\|CREATE_NO_WINDOW/);
assert.match(host, /JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE\|JOB_OBJECT_LIMIT_ACTIVE_PROCESS/);
assert.match(host, /ActiveProcessLimit=1/);
assert.ok(host.indexOf('AssignProcessToJobObject(job,process.hProcess)') < host.indexOf('ResumeThread(process.hThread)'));
assert.match(host, /const std::vector<std::wstring> vars/);
assert.match(host, /GALLIUM_DRIVER=llvmpipe/);
assert.match(host, /require\(renderer&&std::strstr\(renderer,"llvmpipe"\)/);
assert.match(host, /native unmodified mip-zero baseline BEFORE any swap/);
assert.match(host, /glDisable\(GL_DEPTH_TEST\)/);
assert.match(host, /entryLeadsIntoOriginal/);
assert.match(host, /ReadProcessMemory/);
assert.match(host, /glReadPixels/);
assert.match(host, /parametersPersistAfterDraw/);
assert.match(host, /independent-owned-resource-WGL-alternative/);
assert.match(host, /16-times texture gradients select native mip four before hooks/);
assert.match(host, /SamplerParameterf\(biased,0x813a,3.f\);SamplerParameterf\(biased,0x8501,3.f\)/);
assert.match(host, /privateBiasOffExact/);
assert.match(run, /biasFloorDifferenceObserved/);
assert.match(run, /privateBiasMinifiedPixel/);
assert.match(host, /BsiShaders::planGlTextureQuality\(setting,proof,source\)/);
assert.match(host, /require\(!imported,"independent comparison NEVER loads any original DLL"\)/);
assert.match(host, /BindSampler\(0,nativeSampler\);draw\(\);restored=pixel\(\)/);
assert.match(host, /restored==before&&sourceState\(\)==savedSource&&state\(\)==savedState/);
assert.match(run, /\$alternatives.Count -eq 2/);
assert.match(run, /allIndependentCasesPassed=\$alternativesPassed/);
assert.doesNotMatch(host, /\b(?:LoadLibrary[AW]?|FreeLibrary)\s*\(/,
  'No explicit payload load or unsafe unload');
assert.doesNotMatch(host, /^\s*proxy_(?:DirectInput8Create|VerQueryValueW)\s*\(/m,
  'A declared import anchor must never be called');
assert.doesNotMatch(cmake, /add_test\(/);
assert.doesNotMatch(cmake, /find_package\(Qt|target_link_libraries\([^\n]*(?:Qt|MinHook|BSIShaders)/);
assert.match(cmake, /LIBRARY BsiPotatoOriginal\.dll/);
console.log('PASS: Potato is distinct from Z-Lag; its exact five original byte sequences run only by explicit opt-in in fresh outbound-blocked software-WGL children; loading, actual detours, mip pixels and persistent mutation are separate from game acceptance.');
