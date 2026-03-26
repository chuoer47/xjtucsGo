# xjtuClass:线上课堂刷课技巧汇总

## 使用手册1

针对的是 https://gsxjtu.yuketang.cn/pro/courselist 链接的雨课堂

这个雨课堂主要是一些网课。

### 手机夸克浏览器 + 5倍速

算是最保险的方法，也不需要什么操作。

使用手机的夸克浏览器打开视频，调整播放速度即可。

### F12调代码防暂停

其实阻止刷课效率的就是离开页面会暂停。如果可以取消分页，分页刷课，一般来说效率能提升好几倍。如果同时几个设备一起刷，基本一小时拿下全部。

下面给出两个方法：

打开F12在`控制台`输入下面两个代码段之一即可。

```javascript
setInterval(function () {
    var current_video = document.getElementsByTagName('video')[0]
    current_video.play()
}, 100)
```

```javascript
document.hasFocus = true
```

感兴趣具体原理的，可以查看我的[博客详解](https://blog.csdn.net/weixin_64112516/article/details/144062458)

## 使用手册2

针对的是 https://vpahw.xjtu.edu.cn/courses 链接的网课

这个网站非常聪明，使用了超级多的监听器来防止刷课。
（随便说几个：防F12+防切屏+后台记录刷视频个数+防控制台）

我怀疑网站是同学设计的，太懂同学怎么刷题了，一点漏洞都没给留，前端网页设计还巨抽象。

如果你不想太麻烦，就老老实实刷。我现在只发现一个通用的笨方法。

等我后续有时间慢慢摸索出简单可行的方法。

### 手机夸克浏览器 + 5倍速

算是最保险的方法，也不需要什么操作。

使用手机的夸克浏览器打开视频，调整播放速度即可。

