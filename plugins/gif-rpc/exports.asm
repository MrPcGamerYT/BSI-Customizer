; Windows x64 ABI-transparent exports. Do not add a prologue, call, or ret.
; The caller's registers, stack arguments and return address reach System32 intact.
OPTION CASEMAP:NONE
EXTERN bsiVersionExports:QWORD
.code
PUBLIC BSI_GetFileVersionInfoA
BSI_GetFileVersionInfoA PROC
    jmp QWORD PTR [bsiVersionExports + 0]
BSI_GetFileVersionInfoA ENDP
PUBLIC BSI_GetFileVersionInfoByHandle
BSI_GetFileVersionInfoByHandle PROC
    jmp QWORD PTR [bsiVersionExports + 8]
BSI_GetFileVersionInfoByHandle ENDP
PUBLIC BSI_GetFileVersionInfoExA
BSI_GetFileVersionInfoExA PROC
    jmp QWORD PTR [bsiVersionExports + 16]
BSI_GetFileVersionInfoExA ENDP
PUBLIC BSI_GetFileVersionInfoExW
BSI_GetFileVersionInfoExW PROC
    jmp QWORD PTR [bsiVersionExports + 24]
BSI_GetFileVersionInfoExW ENDP
PUBLIC BSI_GetFileVersionInfoSizeA
BSI_GetFileVersionInfoSizeA PROC
    jmp QWORD PTR [bsiVersionExports + 32]
BSI_GetFileVersionInfoSizeA ENDP
PUBLIC BSI_GetFileVersionInfoSizeExA
BSI_GetFileVersionInfoSizeExA PROC
    jmp QWORD PTR [bsiVersionExports + 40]
BSI_GetFileVersionInfoSizeExA ENDP
PUBLIC BSI_GetFileVersionInfoSizeExW
BSI_GetFileVersionInfoSizeExW PROC
    jmp QWORD PTR [bsiVersionExports + 48]
BSI_GetFileVersionInfoSizeExW ENDP
PUBLIC BSI_GetFileVersionInfoSizeW
BSI_GetFileVersionInfoSizeW PROC
    jmp QWORD PTR [bsiVersionExports + 56]
BSI_GetFileVersionInfoSizeW ENDP
PUBLIC BSI_GetFileVersionInfoW
BSI_GetFileVersionInfoW PROC
    jmp QWORD PTR [bsiVersionExports + 64]
BSI_GetFileVersionInfoW ENDP
PUBLIC BSI_VerFindFileA
BSI_VerFindFileA PROC
    jmp QWORD PTR [bsiVersionExports + 72]
BSI_VerFindFileA ENDP
PUBLIC BSI_VerFindFileW
BSI_VerFindFileW PROC
    jmp QWORD PTR [bsiVersionExports + 80]
BSI_VerFindFileW ENDP
PUBLIC BSI_VerInstallFileA
BSI_VerInstallFileA PROC
    jmp QWORD PTR [bsiVersionExports + 88]
BSI_VerInstallFileA ENDP
PUBLIC BSI_VerInstallFileW
BSI_VerInstallFileW PROC
    jmp QWORD PTR [bsiVersionExports + 96]
BSI_VerInstallFileW ENDP
PUBLIC BSI_VerLanguageNameA
BSI_VerLanguageNameA PROC
    jmp QWORD PTR [bsiVersionExports + 104]
BSI_VerLanguageNameA ENDP
PUBLIC BSI_VerLanguageNameW
BSI_VerLanguageNameW PROC
    jmp QWORD PTR [bsiVersionExports + 112]
BSI_VerLanguageNameW ENDP
PUBLIC BSI_VerQueryValueA
BSI_VerQueryValueA PROC
    jmp QWORD PTR [bsiVersionExports + 120]
BSI_VerQueryValueA ENDP
PUBLIC BSI_VerQueryValueW
BSI_VerQueryValueW PROC
    jmp QWORD PTR [bsiVersionExports + 128]
BSI_VerQueryValueW ENDP
END
