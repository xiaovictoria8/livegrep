package server

import (
	"context"
	"fmt"
	"github.com/sourcegraph/jsonrpc2"
	"net"
	"github.com/livegrep/livegrep/server/config"
	"strings"
)

type ClientCapabilities struct{}

type ServerCapabilities struct{}

type InitializeParams struct {
	ProcessId    *int               `json:"processId"`
	RootUri      string             `json:"rootUri"`
	Capabilities ClientCapabilities `json:"capabilities"`
}

type InitializeResult struct {
	Capabilities ServerCapabilities `json:"capabilities"`
}

type TextDocumentIdentifier struct {
	Uri string `json:"uri"`
}

type Position struct {
	Line      int `json:"line"`
	Character int `json:"character"`
}

type TextDocumentPositionParams struct {
	TextDocument TextDocumentIdentifier `json:"textDocument"`
	Position     Position               `json:"position"`
}

type RangeType struct {
	Start Position `json:"start"`
	End   Position `json:"end"`
}

type Location struct {
	Uri       string    `json:"uri"`
	TextRange RangeType `json:"range"`
}

func getLangServerFromFileExt(repo config.RepoConfig, filePath string) *config.LangServer {
	normalizedExt := func(path string) string {
		split := strings.Split(path, ".")
		ext := split[len(split)-1]
		return strings.ToLower(strings.TrimSpace(ext))
	}
	for _, langServer := range repo.LangServers {
		for _, ext := range langServer.Extensions {
			if normalizedExt(filePath) == normalizedExt(ext) {
				return &langServer
			}
		}
	}
	return nil
}

type LangServerClient interface {
	Initialize(params InitializeParams) (InitializeResult, error)
	JumpToDef(params *TextDocumentPositionParams) (Location, error)
}

type langServerClientImpl struct {
	rpcClient *jsonrpc2.Conn
	ctx       context.Context
}

type responseHandler struct {
}

func (r responseHandler) Handle(ctx context.Context, conn *jsonrpc2.Conn, req *jsonrpc2.Request) {
	// TODO
	fmt.Println("Response handler called")
}

func CreateLangServerClient(address string) (client LangServerClient, err error) {
	fmt.Println("create lang server client")
	ctx := context.Background()
	codec := jsonrpc2.VSCodeObjectCodec{}
	conn, err := net.Dial("tcp", address)
	if err != nil {
		return
	}
	handler := responseHandler{}
	rpcConn := jsonrpc2.NewConn(ctx, jsonrpc2.NewBufferedStream(conn, codec), handler)
	client = &langServerClientImpl{
		rpcClient: rpcConn,
		ctx:       ctx,
	}
	fmt.Println("done creating lang server client")
	return client, nil
}

func (c *langServerClientImpl) Initialize(params InitializeParams) (result InitializeResult, err error) {
	fmt.Println("Initialize")
	err = c.rpcClient.Call(c.ctx, "initialize", params, &result)
	fmt.Println("Done initializing")
	if err != nil {
		c.rpcClient.Call(c.ctx, "initialized", nil, nil)
	}
	return result, err
}

func (c *langServerClientImpl) JumpToDef(params *TextDocumentPositionParams) (result Location, err error) {
	fmt.Println("GotoDefRequest")
	err = c.rpcClient.Call(c.ctx, "textDocument/definition", params, &result)
	fmt.Println("Done GotoDefRequest")
	return result, err
}
