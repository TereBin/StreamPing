$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot

Write-Host 'Discord Developer Portal의 OAuth2 > Client Information에서 Client Secret을 복사하세요.'
Write-Host '잠시 후 표시되는 Wrangler 입력란에 직접 붙여넣고 Enter를 누르세요.'
Write-Host ''
npx --yes wrangler@latest secret put DISCORD_CLIENT_SECRET
if ($LASTEXITCODE -ne 0) {
  throw "Wrangler가 오류 코드 $LASTEXITCODE 을(를) 반환했습니다."
}

Write-Host ''
Write-Host '등록 완료. 이 창을 닫아도 됩니다.' -ForegroundColor Green

Read-Host 'Enter를 누르면 창이 닫힙니다'
