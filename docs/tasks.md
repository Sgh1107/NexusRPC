# NexusRPC 鍙墽琛屼换鍔℃竻鍗?

> 浠诲姟鎸変緷璧栭『搴忔帓鍒椼€傛瘡椤瑰畬鎴愬悗蹇呴』琛ユ祴璇曞苟鏇存柊瀵瑰簲鏂囨。銆?

## Phase 0锛氬伐绋嬬洰褰曢鏋?

### TASK-001 寤虹珛椤圭洰鐩綍 `[瀹屾垚]`

- 鐩爣锛氬垱寤?`include/nexus`銆乣src`銆乣proto`銆乣examples`銆乣tests`銆乣benchmarks`銆乣scripts`銆乣tools`銆乣config`銆乣cmake` 鍜?`.github/workflows`銆?
- 杈撳叆锛氬ぇ绾茬洰褰曠粨鏋勩€乣docs/development_decisions.md`銆?
- 杈撳嚭锛氱洰褰曘€佹ā鍧?README銆佹枃浠惰亴璐ｆ竻鍗曘€丆Make 瀛愮洰褰曞叆鍙ｅ崰浣嶃€?
- 渚濊禆锛氭棤銆?
- 楠屾敹锛氱洰褰曠粨鏋勪笌 `docs/design.md` 涓€鑷达紱绌虹洰褰曚娇鐢?`.gitkeep` 鎴?README 淇濈暀锛涗笉鍖呭惈渚濊禆瀹夎鍜屼笟鍔″疄鐜般€?
- 娴嬭瘯锛氬凡瀹屾垚鐩綍鍜屽叧閿枃浠堕潤鎬佹鏌ャ€?
- 椋庨櫓锛氳繃鏃╁垱寤鸿繃澶氱┖鏂囦欢瀵艰嚧鑱岃矗婕傜Щ銆?

### TASK-002 寤虹珛鏋勫缓鐩爣鍗犱綅 `[瀹屾垚]`

- 鐩爣锛氬畾涔?`nexus_net`銆乣nexus_rpc`銆乣nexus_mcp`銆乣nexus_registry`銆乣nexus_observability`銆乪xamples 鍜?tests 鐨?target 杈圭晫銆?
- 杈撳叆锛氱洰褰曢鏋躲€?
- 杈撳嚭锛氶《灞傚拰瀛愮洰褰?CMake 鏂囦欢銆乼arget 渚濊禆鍥俱€?
- 渚濊禆锛歍ASK-001銆?
- 楠屾敹锛氭瀯寤烘枃浠跺彧琛ㄨ揪 target 鍜屾簮鏂囦欢杈圭晫锛屼笉瑕佹眰瀹夎渚濊禆鎴栧畬鎴愮紪璇戦獙璇併€?
- 娴嬭瘯锛氬凡瀹屾垚 CMake 鏂囦欢鍜?target 渚濊禆鏂瑰悜闈欐€佹鏌ワ紱鏈墽琛岀幆澧冮獙璇併€?
- 椋庨櫓锛氭ā鍧楀惊鐜緷璧栥€傚叕鍏辩被鍨嬪簲鏀惧叆浣庡眰鍏叡澶存枃浠躲€?

## Phase 1锛歁VP 缃戠粶涓?RPC

### TASK-010 实现 Buffer 和 EventLoop [代码完成，Buffer 单测通过，EventLoop 待集成测试] 瀹炵幇 Buffer 鍜?EventLoop `[浠ｇ爜瀹屾垚锛屽緟娴嬭瘯]`

- 鐩爣锛氬疄鐜?ET 璇诲啓 Buffer銆乪ventfd 鍞ら啋鍜屼换鍔℃姇閫掋€?
- 杈撳叆锛歀inux epoll API銆?
- 杈撳嚭锛歚include/nexus/net` 鍜?`src/net` 瀹炵幇銆?
- 渚濊禆锛歍ASK-002銆?
- 楠屾敹锛氬凡瀹屾垚 Buffer銆丆hannel銆丒ventLoop 鐨?Linux 瀹炵幇鍜?CMake 鎺ュ叆锛涘崐鍖?绮樺寘銆佽法绾跨▼鎶曢€掑拰鍏抽棴娴佺▼寰呭湪 Linux 鐜楠岃瘉銆?
- 娴嬭瘯锛氭殏鏈墽琛岋紝鎸夌害瀹氱敱鍚庣画寮€鍙戠幆澧冨畬鎴?Buffer 鍗曟祴銆丒ventLoop 绾跨▼娴嬭瘯鍜?ASan銆?
- 椋庨櫓锛欳hannel 鐢熷懡鍛ㄦ湡鍜?EventLoop 鎵€灞炵嚎绋嬩笉涓€鑷淬€?

### TASK-011 瀹炵幇 TCP Server/Connection `[浠ｇ爜瀹屾垚锛孴cpConnection/TcpServer 鏋勯€犲拰鍩烘湰鍥炶皟璁剧疆 7 椤瑰崟鍏冩祴璇曢€氳繃锛岀綉缁?IO 閮ㄥ垎寰呴泦鎴愭祴璇昡`
- 鐩爣锛氫富浠?Reactor銆乤ccept 鍒嗗彂鍜岄潪闃诲璇诲啓銆?
- 渚濊禆锛歍ASK-010銆?
- 杈撳嚭锛歍CP Server API銆丆onnection 鍥炶皟鍜?Echo 楠ㄦ灦銆?
- 楠屾敹锛欵cho 鍙鐞嗗娆¤鍐欍€佹柇杩炲拰浼橀泤鍋滄銆?
- 娴嬭瘯锛歍CP 绔埌绔€佸崐鍖呫€佸苟鍙戣繛鎺ャ€?
- 椋庨櫓锛欵T 妯″紡鏈灏芥暟鎹€犳垚杩炴帴楗ラタ銆?

### TASK-012 瀹炵幇 RPC Frame Codec `[瀹屾垚]`
- 鐩爣锛氬畬鎴?32 瀛楄妭澶淬€乵etadata銆丳rotobuf body 鐨勭紪瑙ｇ爜銆?
- 渚濊禆锛歍ASK-002銆?
- 楠屾敹锛欱ig Endian銆侀暱搴︿笂闄愩€侀潪娉曞瓧娈靛拰淇濈暀瀛楁瑙勫垯绗﹀悎 `docs/protocol.md`銆?
- 娴嬭瘯锛氬崗璁竟鐣屻€佹埅鏂€佹孩鍑恒€佹湭鐭ョ被鍨嬨€?
- 椋庨櫓锛氱洿鎺ュ彂閫?packed struct 鎴栨暣鏁版孩鍑恒€?

### TASK-013 瀹炵幇 RPC Server SDK

- 鐩爣锛氬疄鐜?handler 娉ㄥ唽銆佽姹傚垎鍙戝拰 Unary response銆?
- 渚濊禆锛歍ASK-011銆乀ASK-012銆?
- 楠屾敹锛歚registerService(service, method, handler)` 鍙敞鍐屽苟璋冪敤銆?
- 娴嬭瘯锛氭垚鍔熴€佹湭鎵惧埌銆乭andler 閿欒銆佽秴鏃跺拰寮傚父鏄犲皠銆?
- 椋庨櫓锛氫笟鍔?handler 闃诲 IO 绾跨▼銆?

### TASK-014 瀹炵幇 RPC Client SDK

- 鐩爣锛氬疄鐜板悓姝?call銆乫uture/callback 鍖呰銆乨eadline銆?
- 渚濊禆锛歍ASK-011銆乀ASK-012銆?
- 楠屾敹锛氬彲璋冪敤 Weather 鍜?Echo锛涜繜鍒板搷搴斾笉浼氭薄鏌撳悗缁姹傘€?
- 娴嬭瘯锛氭垚鍔熴€佽繛鎺ュけ璐ャ€佽秴鏃躲€佹柇杩炪€侀噸澶?request ID銆?
- 椋庨櫓锛歷1 鍗曡繛鎺ヤ覆琛岋紝蹇呴』鏄庣‘骞跺彂璋冪敤闄愬埗銆?

### TASK-015 寤虹珛绀轰緥 Proto 鍜屾湇鍔?

- 鐩爣锛氬垱寤?`rpc.proto`銆乣options.proto`銆乣weather.proto`銆乄eather 鍜?Echo 鏈嶅姟銆?
- 渚濊禆锛歍ASK-013銆乀ASK-014銆?
- 楠屾敹锛歐eather 鐩戝惉 `9601`锛孍cho 鐩戝惉 `9602`锛屽潎閫氳繃 SDK 鎻愪緵鏈嶅姟銆?
- 娴嬭瘯锛氱湡瀹?TCP 闂幆銆?
- 椋庨櫓锛氱敓鎴愪唬鐮佸拰鎵嬪啓 Stub 鐨勮亴璐ｆ贩娣嗐€?

## Phase 2锛歁CP stdio MVP

### TASK-020 瀹炵幇 JSON-RPC

- 鐩爣锛氳В鏋愯姹傘€佹瀯閫犲搷搴斿拰閿欒锛屼弗鏍煎鐞?Notification銆?
- 渚濊禆锛歍ASK-002銆?
- 楠屾敹锛氭敮鎸?`initialize`銆乣initialized`銆乣ping`銆侀敊璇爜銆?
- 娴嬭瘯锛氳В鏋愬け璐ャ€佹壒閲忔暟缁勩€侀敊璇?id銆佹棤鍝嶅簲 Notification銆?
- 椋庨櫓锛歴tdout 娣峰叆鏃ュ織鐮村潖 stdio 鍗忚銆?

### TASK-021 瀹炵幇 Protobuf Tool Registry

- 鐩爣锛氫粠鐢熸垚 Descriptor 鍜?options 鐢熸垚 Tool 鍏冩暟鎹€?
- 渚濊禆锛歍ASK-015銆乀ASK-020銆?
- 楠屾敹锛氱敓鎴?`weather.get_current`锛孲chema 浣跨敤 json_name锛屽繀濉潵婧愭纭€?
- 娴嬭瘯锛氬熀纭€绫诲瀷銆佸祵濂椼€乵ap銆乺epeated銆乀imestamp銆亀rapper銆?
- 椋庨櫓锛歰neof 鍜?Any 鎸夊喅绛栭檷绾э紝涓嶅彲浼鎴愬畬鏁存敮鎸併€?

### TASK-022 瀹炵幇 stdio Gateway

- 鐩爣锛氬疄鐜?tools/list銆乼ools/call 鍜?RPC 璺敱銆?
- 渚濊禆锛歍ASK-014銆乀ASK-020銆乀ASK-021銆?
- 楠屾敹锛歁CP Inspector 瀹屾垚 Weather 璋冪敤锛岃繑鍥?text 鍜?structuredContent銆?
- 娴嬭瘯锛氱鍒扮鍜?stderr 鏃ュ織闅旂銆?
- 椋庨櫓锛歁CP request ID 涓?RPC request ID 鏄犲皠閿欒銆?

## Phase 3锛歷1.1 Registry 涓庢不鐞?

### TASK-030 Redis Registry

- 鐩爣锛氬疄鐜?Lua 娉ㄥ唽銆佺画绾︺€佹敞閿€銆乨iscover銆佹竻鐞嗗拰鍏ㄩ噺鍚屾銆?
- 渚濊禆锛歍ASK-015銆乀ASK-002銆?
- 楠屾敹锛氱鍚?`docs/registry.md`锛孯edis 澶辫仈鍙寜缂撳瓨瑙勫垯闄嶇骇銆?
- 娴嬭瘯锛氬瀹炰緥骞跺彂銆乀TL銆丳ub/Sub 閲嶈繛銆丷edis 鏁呴殰娉ㄥ叆銆?
- 椋庨櫓锛氬閲忎簨浠朵涪澶卞悗鏈叏閲忔仮澶嶃€?

### TASK-031 Tool Registry 鍔ㄦ€佹洿鏂?

- 鐩爣锛氬皢 Registry ServiceMeta 鏇存柊杞崲涓烘湰鍦板伐鍏疯〃鍙樺寲銆?
- 渚濊禆锛歍ASK-021銆乀ASK-030銆?
- 楠屾敹锛氫富鍔ㄤ笂涓嬬嚎灏忎簬 1 绉掗€氱煡 SSE Session銆?
- 娴嬭瘯锛氶噸澶嶄簨浠躲€佹棫浜嬩欢銆佸疄渚嬭繃鏈熴€?
- 椋庨櫓锛氬伐鍏烽噸鍚嶏紝闇€瑕佹槑纭啿绐佹嫆缁濈瓥鐣ュ苟璁板綍鏃ュ織銆?

### TASK-032 杩炴帴姹犲拰璐熻浇鍧囪　

- 鐩爣锛氭寜瀹炰緥寤烘睜锛屽姞鍏?RoundRobin 鍜?ConsistentHash銆?
- 渚濊禆锛歍ASK-014銆乀ASK-030銆?
- 楠屾敹锛氬疄渚嬩笅绾垮仠姝㈡柊鍒嗛厤锛屽凡鏈夎皟鐢ㄥ畬鎴愩€?
- 娴嬭瘯锛氬苟鍙?acquire/release銆佸け璐ラ€€閬裤€佺┖闂插洖鏀躲€?
- 椋庨櫓锛氳繛鎺ユ睜閿佺珵浜夊拰杩炴帴鐢熷懡鍛ㄦ湡娉勬紡銆?

### TASK-033 鐔旀柇銆侀檺娴併€侀噸璇?

- 鐩爣锛氬疄鐜拌繛缁け璐ョ啍鏂€佷护鐗屾《銆佸箓绛夐噸璇曘€?
- 渚濊禆锛歍ASK-032銆?
- 楠屾敹锛欻alfOpen 鍙湁涓€涓帰娴嬭姹傦紱闈炲箓绛夋柟娉曚笉鑷姩閲嶈瘯銆?
- 娴嬭瘯锛氱姸鎬佽浆鎹€佺獊鍙戦檺娴併€侀€€閬垮拰閿欒鏄犲皠銆?
- 椋庨櫓锛氬皢涓氬姟閿欒璇垽涓哄彲閲嶈瘯閿欒銆?

### TASK-034 Streamable HTTP

- 鐩爣锛氬疄鐜?HTTP/1.1 POST JSON銆丟ET SSE銆丏ELETE 鍜?Session銆?
- 渚濊禆锛歍ASK-020銆乀ASK-022銆乀ASK-031銆?
- 楠屾敹锛欼nspector 鍜?Claude Desktop 鍧囧彲 initialize銆乴ist銆乧all銆?
- 娴嬭瘯锛歨eader 澶у皬鍐欍€乲eep-alive銆丼ession 杩囨湡銆丼SE 閫氱煡銆?
- 椋庨櫓锛氳嚜鐮?HTTP 瑙ｆ瀽鍣ㄧ殑璇锋眰杈圭晫鍜岄暱杩炴帴娓呯悊銆?

### TASK-035 杩愮淮鎺ュ彛

- 鐩爣锛氬疄鐜?Prometheus 鎸囨爣銆乴ive/ready/detail 鍋ュ悍妫€鏌ュ拰浼橀泤鍋滄銆?
- 渚濊禆锛歍ASK-030銆乀ASK-034銆?
- 楠屾敹锛氬叧闂『搴忓拰 30 绉掔瓑寰呬笂闄愮鍚堣璁°€?
- 娴嬭瘯锛氫緷璧栧紓甯搞€佸仠姝㈡湡闂存柊璇锋眰鍜屾寚鏍囪鏁般€?
- 椋庨櫓锛歳eady 涓?live 鐘舵€佹贩娣嗐€?

## Phase 4锛歷1.2 鎬ц兘涓庡彲闈犳€?

### TASK-040 Benchmark 涓庨暱绋宠剼鏈?

- 鐩爣锛氭彁渚?RPC Echo benchmark銆丳99 鎶ュ憡妯℃澘鍜?7x24 楠屾敹鑴氭湰銆?
- 渚濊禆锛歍ASK-014銆乀ASK-015銆?
- 楠屾敹锛氭姤鍛婅褰曞浐瀹氭祴璇曟潯浠讹紝鑴氭湰鍙惎鍔ㄣ€佸仠姝㈠拰鏀堕泦鏁版嵁銆?
- 娴嬭瘯锛氬皬瑙勬ā smoke benchmark銆?
- 椋庨櫓锛氭妸 MCP 娴嬭瘯缁撴灉涓?RPC Echo 鎸囨爣娣蜂负涓€璋堛€?

### TASK-041 鍙€夊寮?

- 鑼冨洿锛歋nappy/Zstd銆佸畬鏁?JSON Schema銆佸崗浣滃紡 Cancel銆佺湡瀹?progress銆佹粦鍔ㄧ獥鍙ｇ啍鏂€佸璺鐢ㄣ€?
- 渚濊禆锛氬搴?v1.1 鍔熻兘绋冲畾骞舵湁鍩哄噯鏁版嵁銆?
- 楠屾敹锛氭瘡椤瑰寮哄繀椤诲鍔犲崗璁増鏈?鍏煎鎬ц鏄庡拰鍥炲綊娴嬭瘯銆?
- 椋庨櫓锛氬湪娌℃湁鍩哄噯鏁版嵁鍓嶈繘琛屾棤鐩爣浼樺寲銆?
