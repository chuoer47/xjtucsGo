import React, { useState, useEffect } from 'react';
import axios from 'axios';
import { Form, Input, Button, message, Card, Typography, Space, Switch, Tooltip, Divider, Statistic, Row, Col, Modal, List, Tag, Slider, Empty, Tabs, Badge, Avatar, Progress, Alert } from 'antd';
import { 
  UserOutlined, 
  LockOutlined, 
  NumberOutlined, 
  SyncOutlined, 
  HistoryOutlined, 
  BulbOutlined, 
  BulbFilled,
  InfoCircleOutlined,
  CheckCircleOutlined,
  CloseCircleOutlined,
  LoadingOutlined,
  SettingOutlined,
  QrcodeOutlined,
  GithubOutlined,
  WechatOutlined,
  AlipayOutlined,
  ClockCircleOutlined,
  FireOutlined,
  TrophyOutlined,
  HeartOutlined,
  StarOutlined,
  BellOutlined,
  QuestionCircleOutlined,
  DeleteOutlined,
  RestOutlined,
  ReloadOutlined
} from '@ant-design/icons';
import Head from 'next/head';

const { Text, Link, Title, Paragraph } = Typography;
const { TabPane } = Tabs;

const Home = () => {
  const [loading, setLoading] = useState(false);
  const [darkMode, setDarkMode] = useState(false);
  const [history, setHistory] = useState([]);
  const [showHistory, setShowHistory] = useState(false);
  const [form] = Form.useForm();
  const [randomRange, setRandomRange] = useState([10000, 50000]);
  const [useRandom, setUseRandom] = useState(false);
  const [lastUpdate, setLastUpdate] = useState(null);
  const [updateStatus, setUpdateStatus] = useState(null);
  const [activeTab, setActiveTab] = useState('1');
  const [showSettings, setShowSettings] = useState(false);
  const [showHelp, setShowHelp] = useState(false);
  const [autoUpdate, setAutoUpdate] = useState(false);
  const [autoUpdateInterval, setAutoUpdateInterval] = useState(24); // 小时
  const [notificationEnabled, setNotificationEnabled] = useState(true);
  const [streak, setStreak] = useState(0);
  const [showQRCode, setShowQRCode] = useState(false);

  // 从 localStorage 加载历史记录和主题设置
  useEffect(() => {
    const savedHistory = localStorage.getItem('stepsHistory');
    const savedTheme = localStorage.getItem('darkMode');
    const savedAutoUpdate = localStorage.getItem('autoUpdate');
    const savedAutoUpdateInterval = localStorage.getItem('autoUpdateInterval');
    const savedNotificationEnabled = localStorage.getItem('notificationEnabled');
    const savedStreak = localStorage.getItem('streak');
    
    if (savedHistory) {
      setHistory(JSON.parse(savedHistory));
    }
    
    if (savedTheme) {
      setDarkMode(savedTheme === 'true');
    }
    
    if (savedAutoUpdate) {
      setAutoUpdate(savedAutoUpdate === 'true');
    }
    
    if (savedAutoUpdateInterval) {
      setAutoUpdateInterval(parseInt(savedAutoUpdateInterval));
    }
    
    if (savedNotificationEnabled) {
      setNotificationEnabled(savedNotificationEnabled === 'true');
    }
    
    if (savedStreak) {
      setStreak(parseInt(savedStreak));
    }
  }, []);

  // 保存历史记录到 localStorage
  useEffect(() => {
    localStorage.setItem('stepsHistory', JSON.stringify(history));
  }, [history]);

  // 保存主题设置到 localStorage
  useEffect(() => {
    localStorage.setItem('darkMode', darkMode);
    document.body.className = darkMode ? 'dark-mode' : '';
  }, [darkMode]);

  // 保存自动更新设置到 localStorage
  useEffect(() => {
    localStorage.setItem('autoUpdate', autoUpdate);
    localStorage.setItem('autoUpdateInterval', autoUpdateInterval);
    localStorage.setItem('notificationEnabled', notificationEnabled);
  }, [autoUpdate, autoUpdateInterval, notificationEnabled]);

  // 生成随机步数
  const generateRandomSteps = () => {
    const min = randomRange[0];
    const max = randomRange[1];
    const randomSteps = Math.floor(Math.random() * (max - min + 1)) + min;
    form.setFieldsValue({ steps: randomSteps });
  };

  // 清除历史记录
  const clearHistory = () => {
    setHistory([]);
    localStorage.removeItem('stepsHistory');
    message.success('历史记录已清除');
  };

  // 从历史记录中恢复
  const restoreFromHistory = (item) => {
    form.setFieldsValue({
      account: item.account,
      steps: item.steps
    });
    setShowHistory(false);
  };

  // 自动更新步数
  useEffect(() => {
    let timer;
    
    if (autoUpdate && form.getFieldValue('account') && form.getFieldValue('password')) {
      const intervalMs = autoUpdateInterval * 60 * 60 * 1000;
      
      const updateSteps = async () => {
        const values = {
          account: form.getFieldValue('account'),
          password: form.getFieldValue('password'),
          steps: form.getFieldValue('steps') || Math.floor(Math.random() * (randomRange[1] - randomRange[0] + 1)) + randomRange[0]
        };
        
        try {
          const response = await axios.post('/api/update-steps', values);
          
          if (response.data.success) {
            if (notificationEnabled) {
              message.success('自动更新步数成功！');
            }
            
            // 添加到历史记录
            const newHistoryItem = {
              id: Date.now(),
              account: values.account,
              steps: values.steps,
              timestamp: new Date().toLocaleString()
            };
            
            setHistory([newHistoryItem, ...history.slice(0, 9)]);
            setLastUpdate(newHistoryItem);
            
            // 增加连续更新天数
            setStreak(prev => {
              const newStreak = prev + 1;
              localStorage.setItem('streak', newStreak);
              return newStreak;
            });
          }
        } catch (error) {
          if (notificationEnabled) {
            message.error('自动更新失败：' + error.message);
          }
        }
      };
      
      // 立即执行一次
      updateSteps();
      
      // 设置定时器
      timer = setInterval(updateSteps, intervalMs);
    }
    
    return () => {
      if (timer) {
        clearInterval(timer);
      }
    };
  }, [autoUpdate, autoUpdateInterval, form, randomRange, notificationEnabled]);

  const onFinish = async (values) => {
    try {
      setLoading(true);
      setUpdateStatus('loading');
      
      const response = await axios.post('/api/update-steps', values);
      
      if (response.data.success) {
        message.success('步数更新成功！');
        setUpdateStatus('success');
        
        // 添加到历史记录
        const newHistoryItem = {
          id: Date.now(),
          account: values.account,
          steps: values.steps,
          timestamp: new Date().toLocaleString()
        };
        
        setHistory([newHistoryItem, ...history.slice(0, 9)]);
        setLastUpdate(newHistoryItem);
        
        // 增加连续更新天数
        setStreak(prev => {
          const newStreak = prev + 1;
          localStorage.setItem('streak', newStreak);
          return newStreak;
        });
      } else {
        message.error(response.data.message || '更新失败');
        setUpdateStatus('error');
      }
    } catch (error) {
      message.error('请求失败：' + error.message);
      setUpdateStatus('error');
    } finally {
      setLoading(false);
    }
  };

  // 切换暗色模式
  const toggleDarkMode = (checked) => {
    setDarkMode(checked);
    document.body.classList.toggle('dark-mode', checked);
  };

  return (
    <div className={`app-container ${darkMode ? 'dark-mode' : ''}`}>
      <Head>
        <title>小米运动步数修改器</title>
        <meta name="description" content="轻松修改小米运动步数，支持随机生成和历史记录" />
        <link rel="icon" href="/favicon.ico" />
      </Head>

      <div className="background-shapes">
        <div className="shape shape-1"></div>
        <div className="shape shape-2"></div>
        <div className="shape shape-3"></div>
        <div className="shape shape-4"></div>
      </div>
      
      <div className="theme-toggle">
        <Tooltip title={darkMode ? "切换到亮色模式" : "切换到暗色模式"}>
          <Switch
            checked={darkMode}
            onChange={toggleDarkMode}
            checkedChildren={<BulbFilled />}
            unCheckedChildren={<BulbOutlined />}
          />
        </Tooltip>
      </div>
      
      <div className="header-actions">
        <Tooltip title="设置">
          <Button 
            type="text" 
            icon={<SettingOutlined />} 
            onClick={() => setShowSettings(true)}
            className="header-button"
          />
        </Tooltip>
        <Tooltip title="帮助">
          <Button 
            type="text" 
            icon={<QuestionCircleOutlined />} 
            onClick={() => setShowHelp(true)}
            className="header-button"
          />
        </Tooltip>
        <Tooltip title="微信小程序">
          <Button 
            type="text" 
            icon={<QrcodeOutlined />} 
            onClick={() => setShowQRCode(true)}
            className="header-button"
          />
        </Tooltip>
      </div>

      <div className="app-content">
        <div className="glass-card main-card">
          <div className="card-header">
            <Title level={2} className="glass-title">小米运动步数修改器</Title>
            <Text className="glass-subtitle">轻松修改小米运动步数，支持随机生成和历史记录</Text>
          </div>
          
          <Tabs activeKey={activeTab} onChange={setActiveTab} className="glass-tabs">
            <TabPane tab="修改步数" key="1">
              <Form
                form={form}
                name="steps"
                onFinish={onFinish}
                layout="vertical"
                size="large"
                className="glass-form"
              >
                <Row gutter={16}>
                  <Col xs={24} sm={12}>
                    <Form.Item
                      label="账号"
                      name="account"
                      rules={[{ required: true, message: '请输入账号' }]}
                    >
                      <Input 
                        prefix={<UserOutlined />} 
                        placeholder="请输入手机号或邮箱" 
                        className="glass-input"
                      />
                    </Form.Item>
                  </Col>
                  <Col xs={24} sm={12}>
                    <Form.Item
                      label="密码"
                      name="password"
                      rules={[{ required: true, message: '请输入密码' }]}
                    >
                      <Input.Password 
                        prefix={<LockOutlined />} 
                        placeholder="请输入密码" 
                        className="glass-input"
                      />
                    </Form.Item>
                  </Col>
                </Row>

                <Form.Item
                  label={
                    <Space>
                      步数
                      <Switch 
                        size="small" 
                        checked={useRandom} 
                        onChange={setUseRandom} 
                        checkedChildren="随机" 
                        unCheckedChildren="自定义" 
                      />
                    </Space>
                  }
                  name="steps"
                  rules={[{ required: true, message: '请输入步数' }]}
                >
                  <Input 
                    prefix={<NumberOutlined />} 
                    type="number" 
                    placeholder="请输入想要修改的步数" 
                    className="glass-input"
                    disabled={useRandom}
                  />
                </Form.Item>

                {useRandom && (
                  <Form.Item label="随机步数范围">
                    <Slider
                      range
                      min={1000}
                      max={100000}
                      defaultValue={randomRange}
                      onChange={setRandomRange}
                      marks={{
                        1000: '1k',
                        10000: '10k',
                        50000: '50k',
                        100000: '100k'
                      }}
                      className="glass-slider"
                    />
                    <Button 
                      type="dashed" 
                      icon={<SyncOutlined />} 
                      onClick={generateRandomSteps}
                      className="glass-button-secondary"
                    >
                      生成随机步数
                    </Button>
                  </Form.Item>
                )}

                <Form.Item>
                  <Button 
                    type="primary" 
                    htmlType="submit" 
                    loading={loading} 
                    block
                    className="glass-button"
                    icon={updateStatus === 'success' ? <CheckCircleOutlined /> : null}
                  >
                    {loading ? '更新中...' : '更新步数'}
                  </Button>
                </Form.Item>
              </Form>
            </TabPane>
            
            <TabPane tab="历史记录" key="2">
              {history.length > 0 ? (
                <List
                  dataSource={history}
                  renderItem={item => (
                    <List.Item
                      className="glass-list-item"
                      actions={[
                        <Button type="link" onClick={() => restoreFromHistory(item)}>
                          使用
                        </Button>
                      ]}
                    >
                      <List.Item.Meta
                        avatar={<Avatar icon={<UserOutlined />} />}
                        title={`账号: ${item.account}`}
                        description={`更新时间: ${item.timestamp}`}
                      />
                      <Tag color="blue" className="glass-tag">{item.steps} 步</Tag>
                    </List.Item>
                  )}
                />
              ) : (
                <Empty description="暂无历史记录" />
              )}
              
              {history.length > 0 && (
                <Button 
                  danger 
                  onClick={clearHistory}
                  className="clear-history-button"
                >
                  清除历史记录
                </Button>
              )}
            </TabPane>
            
            <TabPane tab="数据统计" key="3">
              <div className="stats-container">
                <Row gutter={[16, 16]}>
                  <Col xs={24} sm={12}>
                    <Card className="glass-card-small">
                      <Statistic 
                        title="连续更新天数" 
                        value={streak} 
                        prefix={<FireOutlined />}
                        suffix="天"
                      />
                    </Card>
                  </Col>
                  <Col xs={24} sm={12}>
                    <Card className="glass-card-small">
                      <Statistic 
                        title="历史记录数" 
                        value={history.length} 
                        prefix={<HistoryOutlined />}
                      />
                    </Card>
                  </Col>
                </Row>
                
                {lastUpdate && (
                  <Card className="glass-card-small last-update-card">
                    <Title level={4}>上次更新</Title>
                    <Row gutter={16}>
                      <Col span={12}>
                        <Statistic 
                          title="步数" 
                          value={lastUpdate.steps} 
                          suffix="步"
                        />
                      </Col>
                      <Col span={12}>
                        <Statistic 
                          title="时间" 
                          value={lastUpdate.timestamp}
                          valueStyle={{ fontSize: '14px' }}
                        />
                      </Col>
                    </Row>
                  </Card>
                )}
                
                {streak > 0 && (
                  <div className="streak-progress">
                    <Text>连续更新进度</Text>
                    <Progress 
                      percent={Math.min((streak / 30) * 100, 100)} 
                      status={streak >= 30 ? "success" : "active"}
                      strokeColor={{
                        '0%': '#108ee9',
                        '100%': '#87d068',
                      }}
                    />
                    <Text type="secondary">
                      {streak >= 30 
                        ? "恭喜！您已达到30天连续更新目标！" 
                        : `再坚持${30 - streak}天，即可达到30天连续更新目标！`}
                    </Text>
                  </div>
                )}
              </div>
            </TabPane>
          </Tabs>
        </div>
        
        <div className="action-buttons">
          <Button 
            icon={<GithubOutlined />} 
            href="https://github.com/miloce/Zepp-Life-Steps" 
            target="_blank"
            className="glass-button-secondary"
          >
            GitHub
          </Button>
          <Button 
            icon={<WechatOutlined />} 
            onClick={() => setShowQRCode(true)}
            className="glass-button-secondary"
          >
            微信小程序
          </Button>
        </div>
        
        <div className="glass-footer">
          <div className="copyright">
            <Text type="secondary">
              © 2025 Zepp-Life-Steps
            </Text>
            <Text type="secondary">
              Powered By <Link href="https://github.com/miloce" target="_blank">Miloce</Link>
            </Text>
          </div>
        </div>
      </div>

      <Modal
        title="设置"
        open={showSettings}
        onCancel={() => setShowSettings(false)}
        footer={[
          <Button key="close" onClick={() => setShowSettings(false)}>
            关闭
          </Button>
        ]}
        width={500}
        className="glass-modal"
      >
        <Form layout="vertical">
          <Form.Item label="自动更新">
            <Switch 
              checked={autoUpdate} 
              onChange={setAutoUpdate}
              checkedChildren="开启" 
              unCheckedChildren="关闭" 
            />
          </Form.Item>
          
          {autoUpdate && (
            <>
              <Form.Item label="更新间隔">
                <Slider
                  min={1}
                  max={48}
                  value={autoUpdateInterval}
                  onChange={setAutoUpdateInterval}
                  marks={{
                    1: '1小时',
                    12: '12小时',
                    24: '24小时',
                    48: '48小时'
                  }}
                />
                <Text type="secondary">每 {autoUpdateInterval} 小时更新一次</Text>
              </Form.Item>
              
              <Form.Item label="通知">
                <Switch 
                  checked={notificationEnabled} 
                  onChange={setNotificationEnabled}
                  checkedChildren="开启" 
                  unCheckedChildren="关闭" 
                />
              </Form.Item>
            </>
          )}
        </Form>
      </Modal>

      <Modal
        title="帮助"
        open={showHelp}
        onCancel={() => setShowHelp(false)}
        footer={[
          <Button key="close" onClick={() => setShowHelp(false)}>
            关闭
          </Button>
        ]}
        width={600}
        className="glass-modal"
      >
        <div className="help-content">
          <Title level={4}>使用前准备</Title>
          <ol>
            <li>下载并注册 Zepp Life（原小米运动）应用</li>
            <li>在微信中启用微信运动功能</li>
            <li>在 Zepp Life 中将账户与微信运动绑定
              <ul>
                <li>进入 Zepp Life 点击"我的"</li>
                <li>选择"第三方接入"</li>
                <li>点击"微信"，最后扫码完成绑定</li>
              </ul>
            </li>
          </ol>
          
          <Title level={4}>注意事项</Title>
          <ul>
            <li>请合理使用，不要频繁修改步数，以免被系统检测</li>
            <li>建议每次修改的步数不要超过合理范围（一般不超过 50000 步）</li>
            <li>请妥善保管您的账号密码，不要分享给他人</li>
            <li>本工具仅供学习和研究使用，请勿用于非法用途</li>
          </ul>
          
          <Alert
            message="免责声明"
            description="本工具仅供学习和研究使用，使用本工具所产生的任何后果由使用者自行承担。我们不对因使用本工具而导致的任何问题负责。"
            type="warning"
            showIcon
          />
        </div>
      </Modal>

      <Modal
        title="微信小程序"
        open={showQRCode}
        onCancel={() => setShowQRCode(false)}
        footer={[
          <Button key="close" onClick={() => setShowQRCode(false)}>
            关闭
          </Button>
        ]}
        width={300}
        className="glass-modal"
      >
        <div className="qrcode-container">
          <img 
            src="https://cdn.jsdelivr.net/gh/miloce/Zepp-Life-Steps/img/MiniProgramCode.png" 
            alt="微信小程序二维码" 
            className="qrcode-image"
          />
          <Text>扫描二维码使用微信小程序版本</Text>
        </div>
      </Modal>
    </div>
  );
};

export default Home; 