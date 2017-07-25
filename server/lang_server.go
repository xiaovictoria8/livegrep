package server

import (
	"net/rpc/jsonrpc"
	"fmt"
)

type requestMessage struct {
	JsonRpc string `json:"jsonrpc"`
	Id      int    `json:"id"`
	Method  string `json:"method"`
	Params  interface{} `json:"params"`
}

type textDocumentIdentifier struct {
	Uri string `json:"uri"`
}

type position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type textDocumentPositionParams struct {
	TextDocument textDocumentIdentifier `json:"textDocument"`
	Position     position `json:"position"`
}

type rangeType struct {
	Start position `json:"start"`
	End   position `json:"end"`
}

type location struct {
	Uri       string `json:"uri"`
	TextRange rangeType `json:"range"`
}

var id int = 0

func RequestLangServer(s *server, clientRequest *GotoDefRequest) {

	langServers := s.config.IndexConfig.LangServers

	address := langServers[0].Address
	rpcClient, err := jsonrpc.Dial("tcp", address)
	if err != nil {
		panic(err)
	}

	id++
	m := requestMessage{
		JsonRpc: "3.0",
		Id:      id,
		Method:  "testDocument/definition",
		Params: textDocumentPositionParams{
			TextDocument: textDocumentIdentifier{
				Uri: clientRequest.FilePath,
			},
			Position: position{
				Line:      clientRequest.Row,
				Character: clientRequest.Col,
			},
		},
	}
	var resp location
	rpcClient.Call("", m, &resp)

	fmt.Println(resp)

}
