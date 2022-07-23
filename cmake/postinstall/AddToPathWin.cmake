#
# Adds %LOCALAPPDATA%/mangetsu to PATH on Windows using pwsh script
#
file(TO_NATIVE_PATH "${INSTALL_BIN_DIR}" WINDIRPATH)
execute_process(COMMAND 
    "powershell.exe" ${WORKINGDIR}/cmake/postinstall/AddToPathWin.ps1 ${WINDIRPATH}
)