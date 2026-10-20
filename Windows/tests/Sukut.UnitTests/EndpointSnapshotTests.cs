using System.Text.Json;
using Sukut;
using Xunit;

public sealed class EndpointSnapshotTests
{
    [Fact]
    public void ReusesUnchangedDevicesButInvalidatesEmptyAndChangedRevisions()
    {
        var cache = new EndpointSnapshotCache();
        Assert.Equal(-1, cache.Version);
        var initial = cache.Adopt(new EngineState { EndpointsVersion = 0, Endpoints = [new() { Id = "one", Name = "Headphones" }] });
        var unchanged = cache.Adopt(new EngineState { EndpointsVersion = 0, State = 1, Recoveries = 4 });
        Assert.Same(initial.Endpoints, unchanged.Endpoints);
        Assert.Equal(4, unchanged.Recoveries); Assert.Equal(1, unchanged.State);
        var removed = cache.Adopt(new EngineState { EndpointsVersion = 1 });
        Assert.Empty(removed.Endpoints); Assert.NotSame(initial.Endpoints, removed.Endpoints);
        var renamed = cache.Adopt(new EngineState { EndpointsVersion = 2, Endpoints = [new() { Id = "one", Name = "New headphones" }] });
        Assert.Equal("New headphones", renamed.Endpoints[0].Name);
        Assert.Equal(2, cache.Version);
    }

    [Fact]
    public void VersionedStatusAvoidsEndpointAllocationWithoutLosingLiveStatus()
    {
        var devices = Enumerable.Range(0, 16).Select(i => new Endpoint { Id = $"device-{i:D3}", Name = $"Speaker or headphone endpoint {i:D3}", Flow = 1 }).ToList();
        byte[] full = JsonSerializer.SerializeToUtf8Bytes(new EngineState { EndpointsVersion = 3, Endpoints = devices, State = 1, PipelineActive = 1 }, SukutJsonContext.Default.EngineState);
        byte[] status = System.Text.Encoding.UTF8.GetBytes("{\"endpointsVersion\":3,\"state\":1,\"pipelineActive\":1,\"recoveries\":7}");
        var cache = new EndpointSnapshotCache();
        cache.Adopt(JsonSerializer.Deserialize(full, SukutJsonContext.Default.EngineState)!);
        for (int i = 0; i < 30; i++) { JsonSerializer.Deserialize(full, SukutJsonContext.Default.EngineState); cache.Adopt(JsonSerializer.Deserialize(status, SukutJsonContext.Default.EngineState)!); }
        long Measure(byte[] json, bool merge)
        {
            long before = GC.GetAllocatedBytesForCurrentThread();
            for (int i = 0; i < 1000; i++)
            {
                var state = JsonSerializer.Deserialize(json, SukutJsonContext.Default.EngineState)!;
                if (merge) { state = cache.Adopt(state); Assert.Equal(16, state.Endpoints.Count); Assert.Equal(7, state.Recoveries); }
            }
            return GC.GetAllocatedBytesForCurrentThread() - before;
        }
        long oldBytes = Measure(full, false), newBytes = Measure(status, true);
        Assert.True(newBytes < oldBytes / 2, $"Full={oldBytes}, versioned={newBytes} bytes / 1000 polls");
        string? report = Environment.GetEnvironmentVariable("SUKUT_ALLOCATION_REPORT");
        if (!string.IsNullOrEmpty(report)) File.WriteAllText(report, JsonSerializer.Serialize(new { polls = 1000, endpoints = 16, baselineBytes = oldBytes, optimizedBytes = newBytes }));
    }
}
