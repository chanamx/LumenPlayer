# 中英、中日四方向翻译原文对照（2026-10-27）

同一组 32 条完整文字输入。这里保存原始输出，供人工复核；并非正式语料榜单。提示词、参数、种子、模型与运行时哈希均在各自 results.json 中。

## en-zh

| 场景 / 输入 | 关键约束 | LFM2.5-350M Q8 | Index 2B Q4 | Hy-MT2 1.8B Q4 |
| --- | --- | --- | --- | --- |
| ordinary / Please turn left at the next traffic light. | next traffic light; left turn | 请在下一条交通信号左转。 | 请在下一个交通灯处左转。 | 在下一个红绿灯处左转。 |
| negation / Do not turn off the computer until the update is complete. | do not turn off; until update complete | 不要在更新完成前关机。 | 在更新完成前，请勿关闭电脑。 | 在更新完成之前，不要关闭电脑。 |
| time / The meeting has moved from 3 p.m. to 4:30 p.m. Please do not be late. | 3 p.m. -> 4:30 p.m.; do not be late | 会议已从下午3点转到下午4:30。请不要迟到。 | 会议时间已调整，从下午3点改为下午4点30分。请不要迟到。 | 会议时间从下午3点改到了下午4:30，请不要迟到。 |
| number / We booked two rooms for three nights and will arrive after midnight. | two rooms; three nights; after midnight | 我们预订了两间房子三晚，后来会到中午。 | 我们预订了两间房间，住三晚，午夜后抵达。 | 我们预订了两间房间，共三晚，将于午夜后到达。 |
| relation / Do not press the red button. Press the blue one twice. | red forbidden; blue twice | 不要按红色按钮。按蓝色按钮两次。 | 不要按红色按钮。请按蓝色按钮两次。 | 不要按红色按钮，请按蓝色按钮两次。 |
| dialogue / It's all right. This isn't your fault. | not your fault | 它是好的。这不是你的责任。 | 没关系，这不是你的错。 | 没关系。这不是你的错。 |
| names / Yamada will meet Li Ming at Tokyo Station tomorrow at 10 a.m. | Yamada; Li Ming; Tokyo Station; tomorrow 10 a.m. | 山田将会在东京站明天上午10点见李明。 | 山田将于明天上午10点在东京站与李明会面。 | 山田明天上午10点在东京站与李明会面。 |
| conditional / If it rains tomorrow, cancel the picnic, but keep the afternoon meeting. | if rain tomorrow; cancel picnic; keep afternoon meeting | 如果明天下雨，取消派对，但保留下午会议。 | 如果明天下雨，就取消野餐，但下午的会议照常进行。 | 如果明天下雨，就取消野餐，但保留下午的会议。 |

## zh-en

| 场景 / 输入 | 关键约束 | LFM2.5-350M Q8 | Index 2B Q4 | Hy-MT2 1.8B Q4 |
| --- | --- | --- | --- | --- |
| ordinary / 请在下一个红绿灯处左转。 | next traffic light; left turn | Turn left at the next red and green light. | Turn left at the next traffic light. | Turn left at the next traffic light. |
| negation / 更新完成之前，不要关闭电脑。 | do not turn off; until update complete | After the update was completed, do not close your computer. | Do not turn off the computer until the update is complete. | Do not turn off the computer before the update is completed. |
| time / 会议从下午三点改到四点半，请不要迟到。 | 3 p.m. -> 4:30 p.m.; do not be late | The meeting starts at 3 PM and ends at 4:30 PM. Do not arrive late. | The meeting has been rescheduled from 3 PM to 4:30 PM. Please do not be late. | The meeting has been changed from 3 p.m. to 4:30 p.m. Please arrive on time. |
| number / 我们预订了两间房，住三晚，午夜后到。 | two rooms; three nights; after midnight | We booked two rooms for three nights, with a late checkout. | We booked two rooms for three nights and will arrive after midnight. | We booked two rooms for three nights, arriving after midnight. |
| relation / 不要按红色按钮，蓝色按钮按两次。 | red forbidden; blue twice | Do not press the red button twice, the blue button once. | Do not press the red button; press the blue button twice. | Don’t press the red button, press the blue button twice. |
| dialogue / 没关系，这不是你的错。 | not your fault | No, that's not your mistake. | It's okay, it's not your fault. | It’s okay, it’s not your fault. |
| names / 山田明天上午十点在东京站与李明见面。 | Yamada; Li Ming; Tokyo Station; tomorrow 10 a.m. | Tomaya will meet Li on the morning of October 10th at the Tokyo Station. | Yamada will meet Li Ming at Tokyo Station at 10 a.m. tomorrow. | Yamada will meet Li Ming at Tokyo Station at 10 a.m. tomorrow. |
| conditional / 如果明天下雨，就取消野餐，但下午的会议照常进行。 | if rain tomorrow; cancel picnic; keep afternoon meeting | If it rains tomorrow, cancel the dinner but the afternoon meeting will still go as planned. | If it rains tomorrow, the picnic will be canceled, but the afternoon meeting will proceed as scheduled. | If it rains tomorrow, the picnic will be canceled, but the afternoon meeting will proceed as usual. |

## ja-zh

| 场景 / 输入 | 关键约束 | LFM2.5-350M Q8 | Index 2B Q4 | Hy-MT2 1.8B Q4 |
| --- | --- | --- | --- | --- |
| ordinary / 次の信号を左に曲がってください。 | next traffic light; left turn | 请左转信号。 | 请向左转下一个信号灯。 | 请向左转弯，进入下一个路口。 |
| negation / 更新が完了するまで、パソコンの電源を切らないでください。 | do not turn off; until update complete | 更新完成后，请不要关闭电源。 | 在更新完成之前，请勿关闭电脑电源。 | 在更新完成之前，请不要关闭电脑的电源。 |
| time / 会議は午後3時から午後4時半に変更されました。遅れないでください。 | 3 p.m. -> 4:30 p.m.; do not be late | 会议从下午3点到下午4点半变成了。请不要迟到。 | 会议时间更改为下午3点到下午4点半。请不要迟到。 | 会议时间从下午3点改到了下午4点半。请不要迟到。 |
| number / 二部屋を三泊予約しました。到着は深夜0時を過ぎます。 | two rooms; three nights; after midnight | 二间房预订了三晚。抵达时将在深夜0点前往。 | 我预订了两间房间，可以住三天。抵达时间将在午夜之后。 | 我预订了两间房，共住三晚。到达时间将在午夜0点之后。 |
| relation / 赤いボタンを押さないでください。青いボタンを2回押してください。 | red forbidden; blue twice | 请不要按红色按钮，按青色按钮两次。 | 请不要按红色按钮。请按蓝色按钮两次。 | 请不要按下红色按钮，请按下蓝色按钮两次。 |
| dialogue / 大丈夫。あなたのせいではありません。 | not your fault | 大好了。你不负责。 | 大丈夫。这不是你的错。 | 没关系。这不是你的错。 |
| names / 山田さんは明日の午前10時に東京駅で李明さんと会います。 | Yamada; Li Ming; Tokyo Station; tomorrow 10 a.m. | 山田先生明天上午10点在东京站与李明会面。 | 山田先生将于明天上午10点在东京站与李明先生会面。 | 山田先生明天上午10点在东京站与李明先生会面。 |
| conditional / 明日雨が降ったらピクニックは中止しますが、午後の会議は予定通り行います。 | if rain tomorrow; cancel picnic; keep afternoon meeting | 明天的雨天会导致聚餐取消，但下午会议计划如期进行。 | 如果明天下雨，野餐活动将取消，但下午的会议照常进行。 | 如果明天下雨，野餐活动将取消，但下午的会议仍按原计划进行。 |

## zh-ja

| 场景 / 输入 | 关键约束 | LFM2.5-350M Q8 | Index 2B Q4 | Hy-MT2 1.8B Q4 |
| --- | --- | --- | --- | --- |
| ordinary / 请在下一个红绿灯处左转。 | next traffic light; left turn | 次の赤緑の交差点を左に曲がってください。 | 次の信号で左折してください。 | 次の信号所で左折してください。 |
| negation / 更新完成之前，不要关闭电脑。 | do not turn off; until update complete | 更新が完了する前には、コンピュータを閉じないでください。 | 更新が完了するまで、コンピューターを閉じないでください。 | 更新が完了する前に、コンピュータを閉じないでください。 |
| time / 会议从下午三点改到四点半，请不要迟到。 | 3 p.m. -> 4:30 p.m.; do not be late | 会議は午後3時に始まり、午後4時半まで続きます。遅刻しないでください。 | 会議は午後3時から4時半に変更されますので、遅刻しないでください。 | 会議は午後3時から4時半に変更されましたので、遅れないでください。 |
| number / 我们预订了两间房，住三晚，午夜后到。 | two rooms; three nights; after midnight | 我们预订了两间房，住三晚，午夜后到。 | 部屋を2つ予約し、3泊します。夜中以降に到着します。 | 私たちは2部屋を予約し、3晩滞在するつもりです。深夜以降に到着します。 |
| relation / 不要按红色按钮，蓝色按钮按两次。 | red forbidden; blue twice | 不要に赤いボタンを押し、青いボタンを2回押しません。 | 赤ボタンは押さないでください。青ボタンを2回押してください。 | 赤いボタンを押さないで、青いボタンを2回押してください。 |
| dialogue / 没关系，这不是你的错。 | not your fault | 無関係ですが、これはあなたの間違えではありません。 | 大丈夫です、あなたのせいではありません。 | 大丈夫です、これはあなたのせいではありません。 |
| names / 山田明天上午十点在东京站与李明见面。 | Yamada; Li Ming; Tokyo Station; tomorrow 10 a.m. | 山田明天午前十点に東京駅で李明と会う。 | 山田は明日の午前10時に東京駅で李明と会います。 | 山田は明日の午前10時に東京駅で李明と会う。 |
| conditional / 如果明天下雨，就取消野餐，但下午的会议照常进行。 | if rain tomorrow; cancel picnic; keep afternoon meeting | 明日雨の日にはディナーをキャンセルしますが、午後の会議は常に進行します。 | 明日雨が降る場合はピクニックを中止しますが、午後の会議は通常通り行います。 | もし明日雨が降ったら、ピクニックを中止しますが、午後の会議は通常通り行われます。 |

