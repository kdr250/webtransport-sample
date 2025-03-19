## 動作確認
### Webサーバー起動
```
python server.py {pem file} {key file}
```

### Windows
ショートカットを作成して、リンク先に以下を指定する
```
"Your\Path\To\chrome.exe" --origin-to-force-quic-on=127.0.0.1:4433 https://localhost:8000/client.html
```

#### 参考URL
- [Zenn - WebTransport のサンプルをローカル環境で動かす in 2022/04](https://zenn.dev/yamayuski/scraps/bbb029e8d292af)
