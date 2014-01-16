#!/usr/bin/env python

import BaseHTTPServer

class Handler (BaseHTTPServer.BaseHTTPRequestHandler) :
	def do_GET (self) :
		data = "Hello World\r\n"

		self.send_response(200, "OK")
		self.send_header("X-Test", "Testing")
		self.send_header('Content-Length', len(data))
		self.end_headers()

		self.wfile.write(data)

	def do_PUT (self) :
		content_length = self.headers.get('Content-Length')
		
		if content_length :
			content_length = int(content_length)

		if content_length :
			data = self.rfile.read(content_length)
		else :
			data = self.rfile.read()

		print data
		
		return self.do_GET()

def main (listen='localhost', port=8080) :
	server = BaseHTTPServer.HTTPServer((listen, port), Handler)
	server.serve_forever()

if __name__ == '__main__' :
	main()
