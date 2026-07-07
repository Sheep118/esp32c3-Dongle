param([string]$Port="COM5", [int]$Minutes=5)
$p = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,One
$p.ReadTimeout = 5000
$p.Open()
$start = Get-Date
$sc = 0; $cc = 0; $tl = 0
while ((Get-Date) - $start -lt [TimeSpan]::FromMinutes($Minutes)) {
    try {
        $l = $p.ReadLine()
        $tl++
        if ($l -match 'abort|Guru|FATAL|PANIC|Restarting|Coredump') {
            $cc++
            Write-Host "!!! CRASH !!! $l" -ForegroundColor Red
        } elseif ($l -match '^\$SCAN_END') {
            $sc++
            $sec = [math]::Round(((Get-Date)-$start).TotalSeconds, 1)
            Write-Host "[${sec}s] $l" -ForegroundColor Green
        }
    } catch {}
}
$elapsed = [math]::Round(((Get-Date)-$start).TotalMinutes, 1)
Write-Host ""
Write-Host "======= ${elapsed}min 测试报告 =======" -ForegroundColor Cyan
Write-Host "扫描周期: ${sc} 次" -ForegroundColor Cyan
Write-Host "总数据行: ${tl} 行" -ForegroundColor Cyan
if ($cc -eq 0) {
    Write-Host "崩溃次数: ${cc} (✅ 无崩溃)" -ForegroundColor Green
} else {
    Write-Host "崩溃次数: ${cc} (❌ 有崩溃!)" -ForegroundColor Red
}
$p.Close()
