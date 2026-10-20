using System.Text;

namespace Sukut;

// Owned exclusively by Engine's STA worker. Error replies reuse the same buffer
// only after the failed reply has been discarded.
internal sealed class NativeReplyBuffer
{
    private byte[] _buffer = new byte[16 * 1024];
    internal int Capacity => _buffer.Length;

    internal ReadOnlySpan<byte> Read(Func<byte[], int, int> call, Func<byte[], int, int> error)
        => ReadCore(call, error, false);

    private ReadOnlySpan<byte> ReadCore(Func<byte[], int, int> call, Func<byte[], int, int> error, bool readingError)
    {
        for (int attempt = 0; attempt < 8; attempt++)
        {
            int length = call(_buffer, _buffer.Length);
            if (length < 0)
            {
                if (readingError) throw new InvalidOperationException("The audio engine error could not be read.");
                throw new InvalidOperationException(Encoding.UTF8.GetString(ReadCore(error, error, true)));
            }
            if (length < 1 || length > 16 * 1024 * 1024)
                throw new InvalidOperationException("The audio engine returned an invalid reply size.");
            if (length > _buffer.Length)
            {
                _buffer = new byte[Math.Max(length, _buffer.Length * 2)];
                continue;
            }
            if (_buffer[length - 1] != 0) throw new InvalidOperationException("The audio engine reply is incomplete.");
            return _buffer.AsSpan(0, length - 1);
        }
        throw new InvalidOperationException("The audio engine reply changed too often.");
    }
}
