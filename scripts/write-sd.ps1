# Scrive un'immagine sulla microSD e la verifica rileggendola.
# Uso (PowerShell da amministratore):
#   write-sd.ps1 -Image D:\Sorgenti\ac100\out\ac100-sd-markersN.img -Disk 1
# Per sicurezza accetta solo dischi USB, non di boot/sistema, sotto i 128 GB.
param(
	[Parameter(Mandatory = $true)][string]$Image,
	[Parameter(Mandatory = $true)][int]$Disk
)
$ErrorActionPreference = 'Stop'

$d = Get-Disk -Number $Disk
if ($d.BusType -ne 'USB' -or $d.IsBoot -or $d.IsSystem -or $d.Size -gt 128GB) {
	throw "Il disco $Disk ($($d.FriendlyName), $($d.BusType), $($d.Size) byte) non sembra una microSD: mi fermo."
}
$imageSize = (Get-Item $Image).Length
if ($imageSize -gt $d.Size) { throw "L'immagine e' piu' grande del disco." }
Write-Host "Scrivo $Image ($imageSize byte) sul disco $Disk ($($d.FriendlyName), $($d.Size) byte)"

# smonta i volumi della scheda, altrimenti Windows blocca la scrittura
Get-Partition -DiskNumber $Disk -ErrorAction SilentlyContinue | Where-Object DriveLetter |
	ForEach-Object { & mountvol "$($_.DriveLetter):" /p | Out-Null }

$bufferSize = 4MB
$buffer = New-Object byte[] $bufferSize
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
	$in = [System.IO.File]::OpenRead($Image)
	$out = New-Object System.IO.FileStream("\\.\PhysicalDrive$Disk",
		[System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite,
		[System.IO.FileShare]::None, $bufferSize, [System.IO.FileOptions]::WriteThrough)
	$written = 0
	while (($n = $in.Read($buffer, 0, $bufferSize)) -gt 0) {
		if ($n % 512) { $n += 512 - ($n % 512) }	# settori interi
		$out.Write($buffer, 0, $n)
		$sha.TransformBlock($buffer, 0, [Math]::Min($n, $imageSize - $written), $null, 0) | Out-Null
		$written += $n
		Write-Progress -Activity "Scrittura" -PercentComplete (100 * $written / $imageSize)
	}
	$sha.TransformFinalBlock((New-Object byte[] 0), 0, 0) | Out-Null
	$expected = [BitConverter]::ToString($sha.Hash)
	$out.Flush()

	# verifica
	$out.Position = 0
	$check = [System.Security.Cryptography.SHA256]::Create()
	$read = 0
	while ($read -lt $imageSize) {
		$n = $out.Read($buffer, 0, $bufferSize)
		$use = [Math]::Min($n, $imageSize - $read)
		$check.TransformBlock($buffer, 0, $use, $null, 0) | Out-Null
		$read += $use
		Write-Progress -Activity "Verifica" -PercentComplete (100 * $read / $imageSize)
	}
	$check.TransformFinalBlock((New-Object byte[] 0), 0, 0) | Out-Null
	$actual = [BitConverter]::ToString($check.Hash)
} finally {
	if ($in) { $in.Close() }
	if ($out) { $out.Close() }
	# fa rileggere la nuova tabella delle partizioni a Windows
	Update-HostStorageCache
}

if ($expected -eq $actual) {
	Write-Host "OK: scrittura verificata ($written byte)." -ForegroundColor Green
} else {
	Write-Host "ERRORE: la verifica non corrisponde!" -ForegroundColor Red
	exit 1
}
