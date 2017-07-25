package server

import (
	"net/rpc/jsonrpc"
	"fmt"
)

type requestMessage struct {
	jsonrpc string `json:"jsonrpc"`
	id      int `json:"id"`
	method  string `json:"method"`
	params  interface{} `json:"params"`
}

type textDocumentIdentifier struct {
	uri string `json:"uri"`
}

type position struct {
	line int `json:"line"`
	character int `json:"character"`
}

type textDocumentPositionParams struct {
	textDocument textDocumentIdentifier `json:"textDocument"`
	position position `json:"position"`
}

type rangeType struct {
	start position `json:"start"`
	end position `json:"end"`
}

type location struct {
	uri string `json:"uri"`
	textRange rangeType `json:"range"`
}

id := 0

func RequestLangServer(s *server, clientRequest *GotoDefRequest) {

	langServers := s.config.IndexConfig.LanguageServers

	address := langServers[0].Address
	rpcClient, err := jsonrpc.Dial("tcp", address)

	id++
	m := requestMessage{
		jsonrpc: "3.0"
		id: id
		method: "testDocument/definition"
		params: textDocumentPositionParams{
			textDocument: textDocumentIdentifier{
				uri: clientRequest.FilePath
			}
			position: position{
				line: clientRequest.Row
				character: clientRequest.Col
			}
		}
	}
	var resp location
	rpcClient.Call("", m, &resp)

	fmt.Println(resp)

}
