# gpdb_ecnu

## 主要思想：

### 采样：

对每个窗口的数据进行随机采样（当需要计算窗口中的数据时，以给定的概率决定是否跳过该数据的函数计算调用），然后调整采样后的数据的窗口函数值来进行拟合用户所需要的窗口函数值（例如sum函数，我们会将最后的结果乘以采样率进行调整来拟合原先的sum函数返回的结果）。

**采样算法示例**

![1.jpg](https://s1.ax1x.com/2018/10/28/ichOvq.png)


	上图是一个具体的采样过程，w1-w4表示4个临近的窗口（由当前行及当前行上下4上数据组成/rows between 4 preceding and 4 following）。每个窗口的采样是一个独立的过程。以第一个窗口为例：
1. 在w1的采样过程中，我们对所有行进行随机采样，第一行及第四行被采样，因此进行调用调用时我们只计算第一和第四行数据。

### 算法优化-增量采样：

由于相邻数据的窗口数据存在重叠（overlap），在优化之后的算法中，我们使用了一个临时窗口（循环使用的链表/数组结构）来保存先前采样到的数据行标，使用了headpos指针指示采样窗口数据的有效头部位置；当窗口的头尾更新时，更新窗口的头部并对新进入到窗口进行采样并添加到临时窗口中；目前临时窗口的实现方式为动态数组（当采样数据过大，超过临时窗口申请的内容地址后就double临时窗口的大小），尚未对临时窗口的大小设定上限。
增量采样算法示例：

![1.jpg](https://s1.ax1x.com/2018/10/28/ichL2n.jpg)

	上图是一个具体的采样计算过程，w1-w4表示4个临近的窗口（由当前行及当前行上下4上数据组成/rows between 4 preceding and 4 following）。观察w1，w2，从第2行到第5行存在overlap。
1. 在w1的采样过程中，我们对所有行进行随机采样，第一行及第四行被采样，相应的行标被存进临时窗口之中；根据调整后的聚合函数计算采样窗口（包括第一行，第四行数据）并返回w1的结果；

2. 窗口滑动到w2。首先我们更新采样窗口中的头部，然后对新加入的数据（第六行数据）进行随机采样，然后根据调整后的聚合函数计算在采样窗口（包括第四行，第六行数据）上进行计算，返回w1的结果；
3. 重复以上过程。

从上面的过程可以看出，由于避免了重叠数据的重复采样，算法的效率得到提高。
实现范围（scope）：
该版本的代码仅对avg进行了优化，其他函数的优化正在继续修改代码。

## 使用方法

(1)使用gpconfig -c enable_sample -v * 指定是否采样， 1 表示采样，0表示不采样

(2)使用gpconfig -c sample_percent -v * 指定采样率


## 修改记录：

(1)修改nodeWindowagg文件：

	(a)添加窗口聚合函数处理分支
		eval_windowaggregates_sample();
	
	(b)修改ExecInitWindowAgg，根据GUC参数调用不同的eval_windowaggregates_*()函数


​	
(2)修改execnode.h

	(a)修改WindowAggState结构体，添加新变量


​	
(5)修改guc_gp.h

(a)	将enable_ttv、enable_sample参数添加到ConfigureNamesInt_gp中

## 参考文献

### 关于该方法的具体介绍和示例请参考
Song G, Qu W, Liu X, et al. Approximate Calculation of Window Aggregate Functions via Global Random Sample[J]. Data Science & Engineering, 2018(2):1-12.

### 关于窗口函数的介绍请参考
PostgreSQL9.3.4中文文档




## 范围：
该版本的代码仅对avg进行了优化，其他函数的优化正在继续修改代码。

## 使用方法
(1)使用gpconfig -c enable_sample -v * 指定是否采样， 1 表示采样，0表示不采样

(2)使用gpconfig -c sample_percent -v * 指定采样率

![]()