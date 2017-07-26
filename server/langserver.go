package server

import (
	"fmt"
	"net/rpc/jsonrpc"
	"github.com/livegrep/livegrep/server/config"
	"strings"
)

type requestMessage struct {
	JsonRpc string      `json:"jsonrpc"`
	Id      int         `json:"id"`
	Method  string      `json:"method"`
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
	Position     position               `json:"position"`
}

type rangeType struct {
	Start position `json:"start"`
	End   position `json:"end"`
}

type location struct {
	Uri       string    `json:"uri"`
	TextRange rangeType `json:"range"`
}

type ClientCapabilities struct{}

type initializeParams struct {
	ProcessId          *int               `'json':"processId"`
	RootUri            string             `'json':"rootUri"`
	ClientCapabilities ClientCapabilities `'json':"clientCapabilities"`
}

var id int = 0

func InitLangServer(langServer config.LangServer, repoConfig config.RepoConfig) bool {
	rpcClient, err := jsonrpc.Dial("tcp", langServer.Address)
	if err != nil {
		fmt.Println(err)
		return false
	}
	langServer.RpcClient = rpcClient
	id++
	params := requestMessage{
		JsonRpc: "3.0",
		Id:      id,
		Method:  "initialize",
		Params: initializeParams{
			ProcessId:          nil,
			RootUri:            repoConfig.Path,
			ClientCapabilities: ClientCapabilities{},
		},
	}
	// todo: read actual response
	var response ClientCapabilities
	fmt.Println("about to init!")
	err = rpcClient.Call("", params, &response)
	fmt.Println("done initting!")
	if err != nil {
		return false
	}
	return true
}

func getLangServerFromFileExt(repo config.RepoConfig, clientRequest *GotoDefRequest) *config.LangServer {
	normalizedExt := func(path string) string {
		split := strings.Split(path, ".")
		ext := split[len(split) - 1]
		return strings.ToLower(strings.TrimSpace(ext))
	}
	for _, langServer := range repo.LangServers {
		for _, ext := range langServer.Extensions {
			if normalizedExt(clientRequest.FilePath) == normalizedExt(ext) {
				return &langServer
			}
		}
	}
	return nil
}

func RequestLangServer(s *server, clientRequest *GotoDefRequest) {

	langServer := getLangServerFromFileExt(s.repos[clientRequest.Repo], clientRequest)

	address := langServer.Address
	fmt.Println(address)
	rpcClient, err := jsonrpc.Dial("tcp", address)
	if err != nil {
		panic(err)
	}

	fmt.Println("dialed")

	id++
	m := requestMessage{
		JsonRpc: "3.0",
		Id:      id,
		Method:  "textDocument/definition",
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
	fmt.Printf("%+v\n", m)
	var resp location
	rpcClient.Call("", m, &resp)

	fmt.Println(resp)

}
