#include "dialogs/creategroupdialog.h"
#include "theme/dialogstyle.h"
#include "theme/thememanager.h"
#include "widgets/avatarlabel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
struct Category { QString label; QStringList children; };
struct Section { QString title; QList<Category> options; };
const QList<Section> kSections = {
    {QStringLiteral("熟人与家校"), {{QStringLiteral("同学"), {}}, {QStringLiteral("同事"), {}}, {QStringLiteral("亲友"), {}}, {QStringLiteral("家校"), {}}}},
    {QStringLiteral("兴趣娱乐"), {{QStringLiteral("游戏"), {}}, {QStringLiteral("二次元"), {}}, {QStringLiteral("更多兴趣"), {QStringLiteral("影视"), QStringLiteral("音乐"), QStringLiteral("星座"), QStringLiteral("运动"), QStringLiteral("读书"), QStringLiteral("摄影"), QStringLiteral("舞蹈"), QStringLiteral("电子产品"), QStringLiteral("汽车"), QStringLiteral("美食"), QStringLiteral("旅游"), QStringLiteral("交友"), QStringLiteral("购物"), QStringLiteral("宠物"), QStringLiteral("健康"), QStringLiteral("兼职"), QStringLiteral("二手闲置"), QStringLiteral("公益"), QStringLiteral("其他")}}}},
    {QStringLiteral("学习交流"), {
        {QStringLiteral("行业交流"), {QStringLiteral("投资"), QStringLiteral("IT/互联网"), QStringLiteral("建筑工程"), QStringLiteral("服务"), QStringLiteral("传媒"), QStringLiteral("营销与广告"), QStringLiteral("教师"), QStringLiteral("律师"), QStringLiteral("公务员"), QStringLiteral("银行"), QStringLiteral("咨询"), QStringLiteral("其他")}},
        {QStringLiteral("学习考试"), {QStringLiteral("托福"), QStringLiteral("雅思"), QStringLiteral("CET 4/6"), QStringLiteral("GRE"), QStringLiteral("GMAT"), QStringLiteral("MBA"), QStringLiteral("考研"), QStringLiteral("高考"), QStringLiteral("中考"), QStringLiteral("职业认证"), QStringLiteral("公务员"), QStringLiteral("其他")}},
        {QStringLiteral("置业安家"), {QStringLiteral("业主"), QStringLiteral("装修"), QStringLiteral("房屋租赁"), QStringLiteral("房屋出售")}},
        {QStringLiteral("品牌产品"), {}}
    }}
};
const QStringList kAvatarIds = {QStringLiteral("blue"), QStringLiteral("green"), QStringLiteral("orange"), QStringLiteral("pink"), QStringLiteral("cyan"), QStringLiteral("purple")};
const QStringList kAvatarTexts = {QStringLiteral("Q"), QStringLiteral("G"), QStringLiteral("C"), QStringLiteral("N"), QStringLiteral("T"), QStringLiteral("群")};
const QStringList kAvatarColors = {QStringLiteral("#12a4ff"), QStringLiteral("#18c98b"), QStringLiteral("#ff9f1a"), QStringLiteral("#fb6f92"), QStringLiteral("#12c9bd"), QStringLiteral("#8b7cf6")};

QPushButton* iconButton(const QString& text, QWidget* parent, const QString& tip) {
    auto* button = new QPushButton(text, parent); button->setObjectName(QStringLiteral("pageIconButton"));
    button->setFixedSize(28, 28); button->setToolTip(tip); return button;
}
void clearLayout(QLayout* layout) {
    while (layout && layout->count()) { QLayoutItem* item = layout->takeAt(0); if (item->layout()) clearLayout(item->layout()); delete item->widget(); delete item; }
}
}

CreateGroupDialog::CreateGroupDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("createGroupDialog")); setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    m_avatarId = kAvatarIds.at(QRandomGenerator::global()->bounded(kAvatarIds.size()));
    setupUi(); updateStyle(); connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &CreateGroupDialog::updateStyle);
}

void CreateGroupDialog::setupUi() {
    auto* root = new QVBoxLayout(this); root->setContentsMargins(0,0,0,0); root->setSpacing(0);
    m_stack = new QStackedWidget(this); root->addWidget(m_stack);
    m_stack->addWidget(buildSelectPage()); m_stack->addWidget(buildCategoryPage()); m_stack->addWidget(buildInfoPage()); showPage(0);
}

QWidget* CreateGroupDialog::buildSelectPage() {
    auto* page = new QWidget(this); auto* row = new QHBoxLayout(page); row->setContentsMargins(0,0,0,0); row->setSpacing(0);
    auto* left = new QWidget(page); left->setObjectName(QStringLiteral("createLeftPane")); left->setFixedWidth(262);
    auto* ll = new QVBoxLayout(left); ll->setContentsMargins(0,0,0,0); ll->setSpacing(0);
    auto* searchHost = new QWidget(left); auto* sl = new QHBoxLayout(searchHost); sl->setContentsMargins(20,14,20,10);
    m_searchEdit = new QLineEdit(searchHost); m_searchEdit->setObjectName(QStringLiteral("dialogInput")); m_searchEdit->setPlaceholderText(QStringLiteral("搜索")); sl->addWidget(m_searchEdit); ll->addWidget(searchHost);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CreateGroupDialog::refreshMemberLists);
    auto* category = new QPushButton(QStringLiteral("按分类创建                         更多  >"), left); category->setObjectName(QStringLiteral("categoryEntry")); category->setFixedHeight(52); ll->addWidget(category);
    connect(category, &QPushButton::clicked, this, [this]{ showPage(1); });
    auto* selectTitle = new QLabel(QStringLiteral("选择好友创建"), left); selectTitle->setObjectName(QStringLiteral("selectTitle")); selectTitle->setFixedHeight(42); selectTitle->setContentsMargins(20,0,0,0); ll->addWidget(selectTitle);
    auto* scroll = new QScrollArea(left); scroll->setObjectName(QStringLiteral("memberScroll")); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll); auto* members = new QVBoxLayout(content); members->setContentsMargins(20,12,20,12); members->setSpacing(6);
    auto* recentTitle = new QLabel(QStringLiteral("v  最近聊天"), content); recentTitle->setObjectName(QStringLiteral("memberSection")); members->addWidget(recentTitle);
    auto* recentHost = new QWidget(content); m_recentLayout = new QVBoxLayout(recentHost); m_recentLayout->setContentsMargins(0,0,0,0); m_recentLayout->setSpacing(2); members->addWidget(recentHost);
    m_recentEmpty = new QLabel(QStringLiteral("暂无最近聊天"), content); m_recentEmpty->setObjectName(QStringLiteral("emptyHint")); m_recentEmpty->setAlignment(Qt::AlignCenter); members->addWidget(m_recentEmpty);
    auto* friendTitle = new QLabel(QStringLiteral("我的好友"), content); friendTitle->setObjectName(QStringLiteral("memberSection")); members->addWidget(friendTitle);
    auto* friendHost = new QWidget(content); m_friendLayout = new QVBoxLayout(friendHost); m_friendLayout->setContentsMargins(0,0,0,0); m_friendLayout->setSpacing(2); members->addWidget(friendHost); members->addStretch();
    scroll->setWidget(content); ll->addWidget(scroll,1); row->addWidget(left);

    auto* right = new QWidget(page); auto* rl = new QVBoxLayout(right); rl->setContentsMargins(20,12,12,12); rl->setSpacing(8);
    auto* header = new QHBoxLayout(); auto* title = new QLabel(QStringLiteral("创建群聊"), right); title->setObjectName(QStringLiteral("pageHeading")); header->addWidget(title); header->addStretch(); auto* close = iconButton(QStringLiteral("X"), right, QStringLiteral("关闭")); header->addWidget(close); connect(close,&QPushButton::clicked,this,&QDialog::reject); rl->addLayout(header);
    auto* selectedHost = new QWidget(right); m_selectedLayout = new QVBoxLayout(selectedHost); m_selectedLayout->setContentsMargins(0,4,0,0); m_selectedLayout->setAlignment(Qt::AlignTop); m_selectedEmpty = new QLabel(QString(), selectedHost); m_selectedLayout->addWidget(m_selectedEmpty); rl->addWidget(selectedHost,1);
    auto* footer = new QHBoxLayout(); footer->addStretch(); m_directCreateBtn = new QPushButton(QStringLiteral("确定"),right); m_directCreateBtn->setObjectName(QStringLiteral("dialogPrimaryBtn")); m_directCreateBtn->setEnabled(false); footer->addWidget(m_directCreateBtn); auto* cancel = new QPushButton(QStringLiteral("取消"),right); cancel->setObjectName(QStringLiteral("dialogSecondaryBtn")); footer->addWidget(cancel); rl->addLayout(footer);
    connect(m_directCreateBtn,&QPushButton::clicked,this,&CreateGroupDialog::finishDirect); connect(cancel,&QPushButton::clicked,this,&QDialog::reject); row->addWidget(right,1); return page;
}

QWidget* CreateGroupDialog::buildCategoryPage() {
    auto* page = new QWidget(this); auto* layout = new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* headerHost = new QWidget(page); headerHost->setObjectName(QStringLiteral("pageHeader")); auto* header = new QHBoxLayout(headerHost); header->setContentsMargins(12,6,12,6); auto* back=iconButton(QStringLiteral("<"),page,QStringLiteral("返回")); header->addWidget(back); header->addStretch(); auto* title=new QLabel(QStringLiteral("按分类创建"),page); title->setObjectName(QStringLiteral("pageHeading")); header->addWidget(title); header->addStretch(); auto* close=iconButton(QStringLiteral("X"),page,QStringLiteral("关闭")); header->addWidget(close); layout->addWidget(headerHost);
    connect(back,&QPushButton::clicked,this,[this]{showPage(0);}); connect(close,&QPushButton::clicked,this,&QDialog::reject);
    auto* scroll=new QScrollArea(page); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); auto* content=new QWidget(scroll); auto* sections=new QVBoxLayout(content); sections->setContentsMargins(12,12,12,12); sections->setSpacing(12);
    for (const Section& section : kSections) {
        auto* card=new QFrame(content); card->setObjectName(QStringLiteral("categoryCard")); auto* cardLayout=new QVBoxLayout(card); cardLayout->setContentsMargins(16,14,16,14); auto* heading=new QLabel(section.title,card); heading->setObjectName(QStringLiteral("sectionHeading")); cardLayout->addWidget(heading); auto* grid=new QGridLayout(); grid->setSpacing(8); cardLayout->addLayout(grid);
        for (int i=0;i<section.options.size();++i) {
            const Category option=section.options.at(i); auto* button=new QPushButton(option.label+(option.children.isEmpty()?QString():QStringLiteral("  v")),card); button->setObjectName(QStringLiteral("categoryTile")); button->setCheckable(!option.children.isEmpty()); grid->addWidget(button,i/4,i%4);
            if (option.children.isEmpty()) connect(button,&QPushButton::clicked,this,[this,option]{chooseCategory(option.label);});
            else { auto* childHost=new QWidget(card); auto* childGrid=new QGridLayout(childHost); childGrid->setContentsMargins(0,8,0,0); childGrid->setSpacing(8); childHost->setVisible(false); for(int c=0;c<option.children.size();++c){const QString child=option.children.at(c);auto* childBtn=new QPushButton(child,childHost);childBtn->setObjectName(QStringLiteral("categoryChild"));childGrid->addWidget(childBtn,c/4,c%4);connect(childBtn,&QPushButton::clicked,this,[this,child]{chooseCategory(child);});} cardLayout->addWidget(childHost); connect(button,&QPushButton::toggled,childHost,&QWidget::setVisible); }
        }
        sections->addWidget(card);
    }
    sections->addStretch(); scroll->setWidget(content); layout->addWidget(scroll,1); return page;
}

QWidget* CreateGroupDialog::buildInfoPage() {
    auto* page=new QWidget(this); auto* layout=new QVBoxLayout(page); layout->setContentsMargins(0,0,0,0); layout->setSpacing(0);
    auto* headerHost=new QWidget(page); headerHost->setObjectName(QStringLiteral("pageHeader")); auto* header=new QHBoxLayout(headerHost); header->setContentsMargins(12,6,12,6); auto* back=iconButton(QStringLiteral("<"),page,QStringLiteral("返回")); header->addWidget(back); header->addStretch(); auto* title=new QLabel(QStringLiteral("填写群信息"),page); title->setObjectName(QStringLiteral("pageHeading")); header->addWidget(title); header->addStretch(); auto* close=iconButton(QStringLiteral("X"),page,QStringLiteral("关闭")); header->addWidget(close); layout->addWidget(headerHost); connect(back,&QPushButton::clicked,this,[this]{showPage(1);}); connect(close,&QPushButton::clicked,this,&QDialog::reject);
    auto* body=new QWidget(page); auto* bl=new QVBoxLayout(body); bl->setContentsMargins(16,12,16,12); bl->addWidget(new QLabel(QStringLiteral("群名称和群头像"),body)); auto* infoCard=new QFrame(body); infoCard->setObjectName(QStringLiteral("infoCard")); auto* il=new QVBoxLayout(infoCard); m_nameEdit=new QLineEdit(infoCard); m_nameEdit->setObjectName(QStringLiteral("dialogInput")); m_nameEdit->setPlaceholderText(QStringLiteral("填写群名称（2-32个字）")); m_nameEdit->setMaxLength(32); il->addWidget(m_nameEdit); il->addWidget(new QLabel(QStringLiteral("群头像"),infoCard)); auto* avatars=new QHBoxLayout(); auto* random=iconButton(QStringLiteral("+"),infoCard,QStringLiteral("随机群头像")); random->setObjectName(QStringLiteral("randomAvatar")); avatars->addWidget(random); m_avatarGroup=new QButtonGroup(this); m_avatarGroup->setExclusive(true);
    for(int i=0;i<kAvatarIds.size();++i){auto* avatar=new QPushButton(kAvatarTexts.at(i),infoCard);avatar->setObjectName(QStringLiteral("avatarChoice"));avatar->setProperty("avatarColor",kAvatarColors.at(i));avatar->setCheckable(true);avatar->setFixedSize(40,40);m_avatarGroup->addButton(avatar,i);avatars->addWidget(avatar); if(kAvatarIds.at(i)==m_avatarId)avatar->setChecked(true);}
    connect(m_avatarGroup,&QButtonGroup::idClicked,this,[this](int id){m_avatarId=kAvatarIds.value(id,kAvatarIds.first());}); connect(random,&QPushButton::clicked,this,[this]{const int id=QRandomGenerator::global()->bounded(kAvatarIds.size());m_avatarId=kAvatarIds.at(id);m_avatarGroup->button(id)->setChecked(true);}); avatars->addStretch(); il->addLayout(avatars); bl->addWidget(infoCard);
    auto* categoryCard=new QFrame(body); categoryCard->setObjectName(QStringLiteral("infoCard")); auto* cl=new QVBoxLayout(categoryCard); auto* categoryRow=new QHBoxLayout(); categoryRow->addWidget(new QLabel(QStringLiteral("群分类"),categoryCard)); categoryRow->addStretch(); m_categoryLabel=new QLabel(categoryCard);m_categoryLabel->setObjectName(QStringLiteral("categoryLabel"));categoryRow->addWidget(m_categoryLabel);cl->addLayout(categoryRow);auto* tags=new QGridLayout();QStringList quick;for(const Section&s:kSections)for(const Category&o:s.options){quick<<o.label;for(const QString&c:o.children)quick<<c;}for(int i=0;i<qMin(10,quick.size());++i){const QString text=quick.at(i);auto* tag=new QPushButton(text,categoryCard);tag->setObjectName(QStringLiteral("categoryTag"));tags->addWidget(tag,i/5,i%5);connect(tag,&QPushButton::clicked,this,[this,text]{m_category=text;m_categoryLabel->setText(text);m_createBtn->setEnabled(m_nameEdit->text().trimmed().size()>=2&&m_agreeCheck->isChecked());});}cl->addLayout(tags);bl->addWidget(categoryCard);bl->addStretch();auto* rule=new QLabel(QStringLiteral("理性追星不盲从，文明表达互尊重，违法违规立举报，社群公约共遵守。"),body);rule->setObjectName(QStringLiteral("ruleText"));rule->setWordWrap(true);bl->addWidget(rule);auto* agreementRow=new QHBoxLayout();m_agreeCheck=new QCheckBox(body);m_agreeCheck->setObjectName(QStringLiteral("agreement"));m_agreeCheck->setChecked(true);agreementRow->addWidget(m_agreeCheck,0,Qt::AlignTop);auto* agreementText=new QLabel(QStringLiteral("已阅读并同意《服务声明》。根据主管部门要求，未成年人禁止担任粉丝群的群主/管理员，经核实将按群相关规则进行处理。"),body);agreementText->setObjectName(QStringLiteral("ruleText"));agreementText->setWordWrap(true);agreementRow->addWidget(agreementText,1);bl->addLayout(agreementRow);layout->addWidget(body,1);
    auto* footerHost=new QWidget(page);footerHost->setObjectName(QStringLiteral("pageFooter"));auto* footer=new QHBoxLayout(footerHost);auto* previous=new QPushButton(QStringLiteral("上一步"),footerHost);previous->setObjectName(QStringLiteral("dialogSecondaryBtn"));footer->addWidget(previous);footer->addStretch();m_createBtn=new QPushButton(QStringLiteral("立即创建"),footerHost);m_createBtn->setObjectName(QStringLiteral("dialogPrimaryBtn"));footer->addWidget(m_createBtn);layout->addWidget(footerHost);connect(previous,&QPushButton::clicked,this,[this]{showPage(1);});auto update=[this]{m_createBtn->setEnabled(m_nameEdit->text().trimmed().size()>=2&&!m_category.isEmpty()&&m_agreeCheck->isChecked());};connect(m_nameEdit,&QLineEdit::textChanged,this,update);connect(m_agreeCheck,&QCheckBox::toggled,this,update);connect(m_createBtn,&QPushButton::clicked,this,&CreateGroupDialog::finishCategorized);update();return page;
}

QWidget* CreateGroupDialog::buildMemberRow(const QString& id,QWidget* parent){auto* row=new QPushButton(parent);row->setObjectName(QStringLiteral("memberRow"));row->setFixedHeight(44);auto* layout=new QHBoxLayout(row);layout->setContentsMargins(4,4,4,4);auto* check=new QLabel(m_selectedIds.contains(id)?QStringLiteral("v"):QString(),row);check->setObjectName(m_selectedIds.contains(id)?QStringLiteral("memberCheckOn"):QStringLiteral("memberCheck"));check->setFixedSize(16,16);check->setAlignment(Qt::AlignCenter);layout->addWidget(check);auto* avatar=new AvatarLabel(row,32);avatar->setTextAvatar(m_names.value(id,id).left(1).toUpper(),ThemeManager::instance()->primaryColor());layout->addWidget(avatar);auto* name=new QLabel(m_names.value(id,id),row);name->setObjectName(QStringLiteral("memberName"));layout->addWidget(name,1);connect(row,&QPushButton::clicked,this,[this,id]{toggleMember(id);});return row;}
void CreateGroupDialog::setCandidateMembers(const QStringList& ids,const QMap<QString,QString>& names,const QStringList& recent){m_memberIds=ids;m_names=names;m_recentIds=recent;refreshMemberLists();}
void CreateGroupDialog::refreshMemberLists(){clearLayout(m_recentLayout);clearLayout(m_friendLayout);const QString q=m_searchEdit?m_searchEdit->text().trimmed():QString();auto match=[this,&q](const QString&id){return q.isEmpty()||id.contains(q,Qt::CaseInsensitive)||m_names.value(id,id).contains(q,Qt::CaseInsensitive);};int recentCount=0;for(const QString&id:m_recentIds)if(match(id)){m_recentLayout->addWidget(buildMemberRow(id,m_recentLayout->parentWidget()));++recentCount;}m_recentEmpty->setVisible(recentCount==0);for(const QString&id:m_memberIds)if(!m_recentIds.contains(id)&&match(id))m_friendLayout->addWidget(buildMemberRow(id,m_friendLayout->parentWidget()));}
void CreateGroupDialog::toggleMember(const QString&id){if(m_selectedIds.contains(id))m_selectedIds.removeAll(id);else m_selectedIds.append(id);refreshMemberLists();refreshSelectedMembers();}
void CreateGroupDialog::refreshSelectedMembers(){clearLayout(m_selectedLayout);for(const QString&id:m_selectedIds){auto* chip=new QPushButton(QStringLiteral("%1  x").arg(m_names.value(id,id)),m_selectedLayout->parentWidget());chip->setObjectName(QStringLiteral("memberChip"));connect(chip,&QPushButton::clicked,this,[this,id]{toggleMember(id);});m_selectedLayout->addWidget(chip);}m_selectedLayout->addStretch();m_directCreateBtn->setEnabled(!m_selectedIds.isEmpty());}
void CreateGroupDialog::chooseCategory(const QString& category){m_category=category;m_categoryLabel->setText(category);if(m_nameEdit->text().trimmed().isEmpty())m_nameEdit->setText(defaultGroupName());showPage(2);}
QString CreateGroupDialog::defaultGroupName()const{QStringList names;for(const QString&id:m_selectedIds.mid(0,3))names<<m_names.value(id,id);return names.isEmpty()?QStringLiteral("新群聊"):names.join(QStringLiteral("、"));}
void CreateGroupDialog::finishDirect(){m_nameEdit->setText(defaultGroupName());emit createRequested(groupName(),m_selectedIds);accept();}void CreateGroupDialog::finishCategorized(){emit createRequested(groupName(),m_selectedIds);accept();}
void CreateGroupDialog::showPage(int page){m_stack->setCurrentIndex(page);if(page==1){resize(600,540);}else resize(538,544);}
QString CreateGroupDialog::groupName()const{return m_nameEdit&&!m_nameEdit->text().trimmed().isEmpty()?m_nameEdit->text().trimmed():defaultGroupName();}QStringList CreateGroupDialog::selectedMembers()const{return m_selectedIds;}QString CreateGroupDialog::selectedCategory()const{return m_category;}QString CreateGroupDialog::selectedAvatarId()const{return m_avatarId;}
void CreateGroupDialog::updateStyle(){auto*tm=ThemeManager::instance();QString avatarRules;for(int i=0;i<kAvatarIds.size();++i)avatarRules+=QStringLiteral("QPushButton#avatarChoice[avatarColor=\"%1\"]{background:%1;color:white;}").arg(kAvatarColors.at(i));setStyleSheet(DialogStyle::common()+QStringLiteral(
"QDialog#createGroupDialog{background:%1;}QWidget#createLeftPane{background:%2;border-right:1px solid %3;}QWidget#pageHeader,QWidget#pageFooter{background:%1;border-bottom:1px solid %3;}QWidget#pageFooter{border-top:1px solid %3;border-bottom:none;}"
"QLabel#pageHeading,QLabel#sectionHeading,QLabel#memberSection,QLabel#memberName{color:%4;font-weight:600;}QLabel#emptyHint,QLabel#ruleText{color:%5;font-size:12px;}QLabel#selectTitle{background:%1;color:%4;border-top:1px solid %3;border-bottom:1px solid %3;font-weight:600;}"
"QPushButton#pageIconButton,QPushButton#randomAvatar{background:transparent;color:%5;border:none;border-radius:5px;}QPushButton#pageIconButton:hover,QPushButton#randomAvatar:hover{background:%6;color:%4;}QPushButton#categoryEntry{background:transparent;color:%4;border:none;text-align:left;padding:0 20px;}QPushButton#categoryEntry:hover{background:%6;}"
"QScrollArea#memberScroll{background:transparent;}QPushButton#memberRow{background:transparent;border:none;border-radius:6px;text-align:left;}QPushButton#memberRow:hover{background:%6;}QLabel#memberCheck,QLabel#memberCheckOn{border:1px solid %3;border-radius:8px;}QLabel#memberCheckOn{background:%7;border-color:%7;color:white;font-size:9px;}"
"QFrame#categoryCard,QFrame#infoCard{background:%1;border:none;border-radius:6px;}QPushButton#categoryTile,QPushButton#categoryChild{background:transparent;color:%4;border:none;border-radius:5px;padding:9px;}QPushButton#categoryTile:hover,QPushButton#categoryTile:checked{background:%2;border:1px solid %4;}QPushButton#categoryChild{background:%2;color:%5;}QPushButton#categoryChild:hover{background:%8;color:%7;}"
"QPushButton#avatarChoice{border:2px solid transparent;border-radius:20px;font-weight:700;}QPushButton#avatarChoice:checked{border-color:%7;}QPushButton#categoryTag,QPushButton#memberChip{background:%2;color:%5;border:none;border-radius:12px;padding:6px 10px;}QPushButton#categoryTag:hover{background:%8;color:%7;}QLabel#categoryLabel{color:%7;}QCheckBox#agreement{color:%5;font-size:12px;spacing:8px;}"
).arg(tm->backgroundColor().name(),tm->backgroundSecondaryColor().name(),tm->borderColor().name(),tm->textColor().name(),tm->textSecondaryColor().name(),tm->backgroundTertiaryColor().name(),tm->primaryColor().name(),tm->primarySoftColor().name())+avatarRules);}
