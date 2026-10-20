using Microsoft.Diagnostics.Tracing;
using System.Text.Json;
var large = new List<object>();
int ticks=0;
using var source = new EventPipeEventSource(args[0]);
source.Clr.GCAllocationTick += data =>
{
    if(data.TimeStampRelativeMSec < 60_000)return;
    ticks++;
    if((int)data.AllocationKind==1)large.Add(new{time_ms=data.TimeStampRelativeMSec,type=data.TypeName,bytes=data.AllocationAmount64});
};
source.Process();
File.WriteAllText(args[1],JsonSerializer.Serialize(new{settle_seconds=60,allocation_ticks=ticks,large_object_allocation_ticks=large.Count,large},new JsonSerializerOptions{WriteIndented=true}));
Console.WriteLine($"Observed {ticks} allocation ticks after settling; {large.Count} large-object ticks.");
