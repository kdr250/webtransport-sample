## コマンド
### Python版のWebTransportサーバー起動
```
python main.py {pem file} {key file}
```

### CLIでQUICにアクセスする
```
cat "$(mkcert -CAROOT)/rootCA.pem" >> .venv/lib/python3.12/site-packages/certifi/cacert.pem
python3 connect_quic.py .venv/lib/python3.12/site-packages/certifi/cacert.pem
```
