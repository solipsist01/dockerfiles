#Omdat we speciale sneeuwvlokjes zijn, willen we onze eigen aanpassingen.
#de standaard entrypoint voorziet in dynamische certificates, als environmental variables encoded in base64, die weggeschreven worden in certificate files.
#wanneer je niets supplied, worden die certs overschreven door nieuw gegenereerde self signed. je MOET dus certificaten doorpassen. al bestaande files gaat niet werken.
#
#wat wij willen, is een let's encrypt container die files genereerd met de juiste certs.
#dit script gaat de files die gemaakt zijn door let's encrypt, converten naar base64 strings, en deze voeden aan de standaard entrypoint scripts die devolutions heeft gemaakt.
#deze 'hack' wordt erbij geplaatst, en we passen het image van devolutions aan, zodat eerst deze customization draait, en daarna pas entrypoint.ps1. capiche?
#
#Het duurt een tijdje wittewel, voordat de certs gegenereerd zijn. 10 seconden ofzo.
#We blijven oneinding lang zoeken naar valide certs, en anders doen we lekker niks.

function Convert-ToBase64 {
    param (
        [Parameter(Mandatory = $true, ValueFromPipeline = $true)]
        [string]$InputString
    )

    begin {
        $builder = New-Object System.Text.StringBuilder
    }
    process {
        [void]$builder.AppendLine($InputString)
    }
    end {
        $text = $builder.ToString().TrimEnd()
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
        [System.Convert]::ToBase64String($bytes)
    }
}

if ($Env:CLEAN_CERTS -eq $true) {
    write-host "Cleaning certificates as requested."
    if ($(Test-Path -path "/tmp/devolutions-gateway/server.crt")) {remove-item '/tmp/devolutions-gateway/server.crt' -Force }
    if ($(Test-Path -path "/tmp/devolutions-gateway/server.key")) {remove-item '/tmp/devolutions-gateway/server.key' -Force }
}

if (($Env:WEB_SCHEME -eq 'https') -and (!$Env:TLS_PRIVATE_KEY_B64 -or !$Env:TLS_CERTIFICATE_B64)) {
    do {
        $certificatefiles = Get-ChildItem -Path '/certs' -Recurse -File -Filter *.pem | where-object {$_ -match 'live'}
        $Env:TLS_PRIVATE_KEY_B64 = $certificatefiles | where-object {$_.Name -eq 'privkey.pem'} | Get-Content | Convert-ToBase64
        $Env:TLS_CERTIFICATE_B64 = $certificatefiles | where-object {$_.Name -eq 'fullchain.pem'} | Get-Content | Convert-ToBase64
        Write-Host "Waiting for private key and full chain..."
        Start-Sleep -Seconds 2
    } while (!$Env:TLS_PRIVATE_KEY_B64 -or !$Env:TLS_CERTIFICATE_B64)
}
. entrypoint.ps1

