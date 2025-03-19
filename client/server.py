import argparse
from http.server import HTTPServer, SimpleHTTPRequestHandler
import ssl

def run(host, port, context, handler):
    server = HTTPServer((host, port), handler)
    server.socket = context.wrap_socket(server.socket)
    print(f'Server listening on {host}:{port}')

    try:
        server.serve_forever()
    except:
        pass
    server.server_close()
    print('Server stops')

if __name__ == '__main__':
    host = 'localhost'
    port = 8000

    parser = argparse.ArgumentParser()
    parser.add_argument('certificate')
    parser.add_argument('key')
    args = parser.parse_args()

    context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
    context.load_cert_chain(args.certificate, keyfile=args.key)
    context.options |= ssl.OP_NO_TLSv1 | ssl.OP_NO_TLSv1_1
    handler = SimpleHTTPRequestHandler

    run(host, port, context, handler)
