# Ask the running firmware to reboot into the STM32 DFU bootloader.
# Finds the USB CDC port (VID 0483, PID 5740) and does a "1200 baud touch":
# open the port at 1200 baud with DTR low, then close it.
# Exit code: 0 = request sent, 1 = no board port found or port busy.

$port = Get-CimInstance Win32_PnPEntity -Filter "PNPDeviceID LIKE 'USB\\VID_0483&PID_5740%'" |
    ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } } |
    Select-Object -First 1

if (-not $port) {
    Write-Host "No board COM port (VID 0483 PID 5740) found."
    exit 1
}

Write-Host "Requesting DFU on $port (1200 baud touch)..."
try {
    $sp = New-Object System.IO.Ports.SerialPort $port, 1200
    $sp.DtrEnable = $false
    $sp.Open()
    Start-Sleep -Milliseconds 100
    $sp.Close()
} catch {
    Write-Host "Cannot open ${port}: $($_.Exception.Message)"
    Write-Host "Close any terminal that uses this port and try again."
    exit 1
}
exit 0
