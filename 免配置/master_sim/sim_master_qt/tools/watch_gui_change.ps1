Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$change = -join @(0x53D8, 0x4F4D | ForEach-Object { [char]$_ })
$title = -join @(0x514D, 0x914D, 0x7F6E, 0x4EFF, 0x771F, 0x4E3B, 0x7AD9 | ForEach-Object { [char]$_ })

$root = [System.Windows.Automation.AutomationElement]::RootElement
$winCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::NameProperty, $title)
$tableCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::Table)
$dataItemCond = New-Object System.Windows.Automation.PropertyCondition(
    [System.Windows.Automation.AutomationElement]::ControlTypeProperty,
    [System.Windows.Automation.ControlType]::DataItem)

$deadline = (Get-Date).AddMinutes(20)
while ((Get-Date) -lt $deadline) {
    try {
        $win = $root.FindFirst([System.Windows.Automation.TreeScope]::Children, $winCond)
        if ($win) {
            $table = $win.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $tableCond)
            if ($table) {
                $items = $table.FindAll([System.Windows.Automation.TreeScope]::Descendants, $dataItemCond)
                $names = @()
                foreach ($i in $items) { $names += $i.Current.Name }
                for ($k = 1; $k -lt $names.Count; $k++) {
                    if ($names[$k] -eq $change) {
                        $start = $k - 1
                        $row = $names[$start..([Math]::Min($start + 8, $names.Count - 1))]
                        'CHANGE_EVENT: ' + ($row -join ' | ')
                        exit 0
                    }
                }
            }
        }
    } catch {
    }
    Start-Sleep -Seconds 2
}
'NO_CHANGE_EVENT'
