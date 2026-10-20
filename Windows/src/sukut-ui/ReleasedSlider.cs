using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using VirtualKey = Windows.System.VirtualKey;

namespace Sukut;

// ValueChanged remains useful for the visual preview. Consumers apply audio
// only from ValueCommitted, after pointer/key interaction has finished.
public sealed class ReleasedSlider : Slider
{
    private readonly HashSet<uint> _pointers = new();
    private readonly HashSet<VirtualKey> _keys = new();
    private bool _queued;
    private double _committedValue;
    public bool IsInteracting => _pointers.Count != 0 || _keys.Count != 0;
    public event EventHandler? ValueCommitted;

    public ReleasedSlider()
    {
        AddHandler(PointerPressedEvent, new PointerEventHandler((_, e) => _pointers.Add(e.Pointer.PointerId)), true);
        AddHandler(PointerReleasedEvent, new PointerEventHandler(FinishPointer), true);
        AddHandler(PointerCanceledEvent, new PointerEventHandler(FinishPointer), true);
        AddHandler(PointerCaptureLostEvent, new PointerEventHandler(FinishPointer), true);
        PreviewKeyDown += (_, e) => { if (AdjustsValue(e.Key)) _keys.Add(e.Key); };
        AddHandler(KeyUpEvent, new KeyEventHandler((_, e) => { if (_keys.Remove(e.Key)) QueueCommit(); }), true);
        LostFocus += (_, _) => { _keys.Clear(); QueueCommit(); };
        Unloaded += (_, _) => { _pointers.Clear(); _keys.Clear(); QueueCommit(); };
        ValueChanged += (_, _) => QueueCommit();
    }
    private static bool AdjustsValue(VirtualKey key) => key is VirtualKey.Left or VirtualKey.Right
        or VirtualKey.Up or VirtualKey.Down or VirtualKey.Home or VirtualKey.End
        or VirtualKey.PageUp or VirtualKey.PageDown;
    private void FinishPointer(object sender, PointerRoutedEventArgs e)
    {
        _pointers.Remove(e.Pointer.PointerId); QueueCommit();
    }
    private void QueueCommit()
    {
        if (_queued) return;
        _queued = true;
        // Deferring also lets handled Thumb/track input reach the slider before
        // deciding whether a ValueChanged notification is a completed action.
        DispatcherQueue.TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            _queued = false;
            if (IsInteracting || Value == _committedValue) return;
            _committedValue = Value;
            ValueCommitted?.Invoke(this, EventArgs.Empty);
        });
    }
}
