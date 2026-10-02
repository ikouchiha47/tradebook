Learn:

- bid / ask
- spread
- limit order
- market order
- cancel
- replace
- execution
- order book
- price-time priority
- market data
- sequence numbers
- latency
- throughput

Later, basic probability/statistics becomes useful:

- distributions
- mean/variance
- correlation
- conditional probability
- hypothesis testing
- time series

Progression:

```
1. ITCH file
      │
      ▼
2. Event stream abstraction
      │
      ▼
3. Nasdaq order-book reconstruction
      │
      ├── sequence numbers
      ├── add
      ├── execute
      ├── cancel
      ├── replace
      └── consistency checks
      │
      ▼
4. Matching engine
      │
      ├── limit orders
      ├── market orders
      ├── cancel
      ├── replace
      └── executions
      │
      ▼
5. Event log + replay
      │
      ▼
6. Performance laboratory
      │
      ├── latency
      ├── CPU
      ├── cache
      ├── allocation
      ├── locks
      └── networking
      │
      ▼
7. Failure injection
      │
      ├── packet loss
      ├── retransmissions
      ├── queue buildup
      ├── retry storms
      ├── consumer lag
      └── corrupted/out-of-order events
      │
      ▼
8. Live crypto adapter
      │
      ├── WebSocket market data
      ├── REST
      ├── reconnect
      ├── snapshots
      └── sequence recovery
      │
      ▼
9. Paper trading system
```
