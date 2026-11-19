import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
export function violations(root) {
  const found = [];
  function walk(dir) {
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
      const file = path.join(dir, entry.name);
      if (entry.isDirectory()) walk(file);
      else if (entry.isFile() && file.endsWith('.h')) {
        const text = fs.readFileSync(file, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '');
        const relative = path.relative(root, file).split(path.sep).join('/');
        const sdkInclude = /^\s*#\s*include\s*[<"](?:windows|shlobj)\.h[>"]/im.test(text);
        let code = text.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, '');
        // Existing ABI-compatible forward declaration does not depend on SDK headers.
        // Exempt this one symbol, never the entire file or an SDK include.
        if (relative === 'juce_shell/src/live_compare/LiveCompareChainTiming.h')
          code = code.replace(/\bGetCurrentThreadId\b/g, '');
        const sdkToken = /\b(?:HANDLE|HMODULE|HINSTANCE|HWND|HKEY|HDC|HRESULT|BOOL|DWORD|WORD|BYTE|UINT|LONG|LONGLONG|ULONG|FILETIME|LARGE_INTEGER|ULARGE_INTEGER|BY_HANDLE_FILE_INFORMATION|FILE_BASIC_INFO|REFKNOWNFOLDERID|PWSTR|LPCWSTR|INVALID_HANDLE_VALUE|GENERIC_READ|GENERIC_WRITE|FILE_ATTRIBUTE_\w+|FILE_FLAG_\w+|FILE_SHARE_\w+|CREATE_NEW|OPEN_ALWAYS|OPEN_EXISTING|THREAD_PRIORITY_\w+|PAGE_READWRITE|CreateFileW|CloseHandle|GetFileInformationByHandle|GetFileInformationByHandleEx|SetFileTime|ReadFile|SetFilePointerEx|GetFileAttributesW|SHGetKnownFolderPath|CoTaskMemFree|GetModuleHandleExW|CreateFileMappingW|GetLastError|GetThreadTimes|GetCurrentThread|SetThreadPriority|GetCurrentThreadId)\b/;
        if (sdkInclude || sdkToken.test(code)) found.push(relative);
      }
    }
  }
  walk(path.join(root, 'juce_shell/src'));
  return found.sort();
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const root = fileURLToPath(new URL('../', import.meta.url));
  const bad = violations(root);
  if (bad.length) { console.error('Windows SDK header boundary failed:\n' + bad.join('\n')); process.exitCode = 1; }
  else console.log('Windows SDK header boundary: PASS');
}
