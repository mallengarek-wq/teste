using System.Net;
using System.Net.Http.Headers;
using System.Net.Sockets;
using System.Text;

const int Port = 55355;
const string Product = "RA-Bridge v0.7";

using var singleInstance = new Mutex(true, "Local\\RA-PSP-Bridge-55355", out bool createdNew);
if (!createdNew)
{
    Console.WriteLine("RA-Bridge ja esta em execucao.");
    return;
}

var handler = new HttpClientHandler
{
    AllowAutoRedirect = true,
    AutomaticDecompression = DecompressionMethods.GZip | DecompressionMethods.Deflate | DecompressionMethods.Brotli
};
using var http = new HttpClient(handler)
{
    Timeout = TimeSpan.FromSeconds(20)
};
http.DefaultRequestHeaders.UserAgent.ParseAdd("RA-PSP-Bridge/0.7");

var listener = new TcpListener(IPAddress.Loopback, Port);
listener.Start(16);
Console.Title = Product;
Console.WriteLine($"{Product} ativo em 127.0.0.1:{Port}");
Console.WriteLine("Deixe esta janela aberta enquanto estiver usando o RA-PSP no PPSSPP.");
Console.WriteLine();

while (true)
{
    TcpClient client;
    try
    {
        client = await listener.AcceptTcpClientAsync();
    }
    catch (Exception ex)
    {
        Console.WriteLine($"Falha ao aceitar conexao local: {ex.Message}");
        continue;
    }

    _ = Task.Run(() => HandleClientAsync(client, http));
}

static async Task HandleClientAsync(TcpClient client, HttpClient http)
{
    using (client)
    using (var stream = client.GetStream())
    {
        try
        {
            var request = await ReadRequestAsync(stream);
            if (request == null)
            {
                await WriteResponseAsync(stream, 400, "text/plain; charset=utf-8", "Bad request");
                return;
            }

            if (request.Path.Equals("/health", StringComparison.OrdinalIgnoreCase))
            {
                await WriteResponseAsync(stream, 200, "text/plain; charset=utf-8", "RA-Bridge OK");
                return;
            }

            if (!request.Path.Equals("/ra", StringComparison.OrdinalIgnoreCase))
            {
                await WriteResponseAsync(stream, 404, "text/plain; charset=utf-8", "Not found");
                return;
            }

            if (!request.Headers.TryGetValue("x-ra-target", out var targetText) ||
                !Uri.TryCreate(targetText, UriKind.Absolute, out var target) ||
                !target.Scheme.Equals(Uri.UriSchemeHttps, StringComparison.OrdinalIgnoreCase) ||
                !IsAllowedHost(target.Host))
            {
                await WriteResponseAsync(stream, 400, "application/json", "{\"Success\":false,\"Error\":\"Invalid RA bridge target\"}");
                return;
            }

            request.Headers.TryGetValue("x-ra-method", out var methodText);
            var method = string.Equals(methodText, "GET", StringComparison.OrdinalIgnoreCase) ? HttpMethod.Get : HttpMethod.Post;

            using var upstream = new HttpRequestMessage(method, target);
            if (method != HttpMethod.Get)
            {
                upstream.Content = new ByteArrayContent(request.Body);
                if (request.Headers.TryGetValue("x-ra-content-type", out var contentType) && !string.IsNullOrWhiteSpace(contentType))
                {
                    upstream.Content.Headers.ContentType = MediaTypeHeaderValue.Parse(contentType);
                }
                else
                {
                    upstream.Content.Headers.ContentType = new MediaTypeHeaderValue("application/x-www-form-urlencoded");
                }
            }

            using var response = await http.SendAsync(upstream, HttpCompletionOption.ResponseContentRead);
            var bytes = await response.Content.ReadAsByteArrayAsync();
            var responseType = response.Content.Headers.ContentType?.ToString() ?? "application/json";

            Console.WriteLine($"{DateTime.Now:HH:mm:ss} {(int)response.StatusCode} {target.Host}{target.AbsolutePath}");
            await WriteResponseAsync(stream, (int)response.StatusCode, responseType, bytes);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"Bridge error: {ex.GetType().Name}: {ex.Message}");
            var safe = "{\"Success\":false,\"Error\":\"RA-Bridge HTTPS upstream failed\"}";
            try { await WriteResponseAsync(stream, 503, "application/json", safe); } catch { }
        }
    }
}

static bool IsAllowedHost(string host)
{
    return host.Equals("retroachievements.org", StringComparison.OrdinalIgnoreCase) ||
           host.EndsWith(".retroachievements.org", StringComparison.OrdinalIgnoreCase);
}

sealed class LocalRequest
{
    public string Path { get; init; } = "/";
    public Dictionary<string, string> Headers { get; init; } = new(StringComparer.OrdinalIgnoreCase);
    public byte[] Body { get; init; } = Array.Empty<byte>();
}

static async Task<LocalRequest?> ReadRequestAsync(NetworkStream stream)
{
    const int MaxHeader = 32 * 1024;
    using var headerBuffer = new MemoryStream();
    var one = new byte[1];
    int matched = 0;

    while (headerBuffer.Length < MaxHeader)
    {
        int n = await stream.ReadAsync(one);
        if (n <= 0) return null;
        headerBuffer.WriteByte(one[0]);

        matched = one[0] switch
        {
            (byte)'\r' when matched == 0 => 1,
            (byte)'\n' when matched == 1 => 2,
            (byte)'\r' when matched == 2 => 3,
            (byte)'\n' when matched == 3 => 4,
            _ => one[0] == (byte)'\r' ? 1 : 0
        };
        if (matched == 4) break;
    }

    if (matched != 4) return null;
    var headerText = Encoding.ASCII.GetString(headerBuffer.ToArray());
    var lines = headerText.Split(new[] { "\r\n" }, StringSplitOptions.None);
    if (lines.Length == 0) return null;

    var first = lines[0].Split(' ', StringSplitOptions.RemoveEmptyEntries);
    if (first.Length < 2) return null;

    var headers = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
    for (int i = 1; i < lines.Length; i++)
    {
        var line = lines[i];
        if (string.IsNullOrEmpty(line)) break;
        int colon = line.IndexOf(':');
        if (colon <= 0) continue;
        headers[line[..colon].Trim()] = line[(colon + 1)..].Trim();
    }

    int contentLength = 0;
    if (headers.TryGetValue("Content-Length", out var lengthText))
        int.TryParse(lengthText, out contentLength);
    if (contentLength < 0 || contentLength > 1024 * 1024) return null;

    var body = new byte[contentLength];
    int read = 0;
    while (read < contentLength)
    {
        int n = await stream.ReadAsync(body.AsMemory(read, contentLength - read));
        if (n <= 0) return null;
        read += n;
    }

    return new LocalRequest { Path = first[1], Headers = headers, Body = body };
}

static Task WriteResponseAsync(NetworkStream stream, int status, string contentType, string text) =>
    WriteResponseAsync(stream, status, contentType, Encoding.UTF8.GetBytes(text));

static async Task WriteResponseAsync(NetworkStream stream, int status, string contentType, byte[] body)
{
    string reason = status switch
    {
        200 => "OK",
        400 => "Bad Request",
        404 => "Not Found",
        503 => "Service Unavailable",
        _ => "Response"
    };

    var header = Encoding.ASCII.GetBytes(
        $"HTTP/1.1 {status} {reason}\r\n" +
        $"Content-Type: {contentType}\r\n" +
        $"Content-Length: {body.Length}\r\n" +
        "Connection: close\r\n" +
        "Cache-Control: no-store\r\n\r\n");

    await stream.WriteAsync(header);
    if (body.Length > 0) await stream.WriteAsync(body);
    await stream.FlushAsync();
}
