/****************************************************************************
**
** Copyright (c) 2014 Jolla Ltd.
** Contact: Raine Makelainen <raine.makelainen@jolla.com>
**
****************************************************************************/

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "webpagequeue.h"
#include "declarativewebpage.h"

#include <QObject>
#include <QRectF>

#ifndef DEBUG_LOGS
#define DEBUG_LOGS 0
#endif

#if DEBUG_LOGS
#include <QDebug>
#endif

WebPageQueue::WebPageQueue()
    : m_maxLiveCount(5)
    , m_livePagePrepended(false)
{
}

WebPageQueue::~WebPageQueue()
{
    clear();
}

int WebPageQueue::count() const
{
    int count = 0;
    for (; count < m_queue.count() && m_queue.at(count)->webPage; ++count) {}
    return count;
}

bool WebPageQueue::alive(int tabId) const
{
    int index = -1;
    WebPageQueue::WebPageEntry *webPageEntry = find(tabId, index);
    return index >= 0 && webPageEntry && webPageEntry->webPage;
}

bool WebPageQueue::active(int tabId) const
{
    if (!m_queue.isEmpty()) {
        WebPageQueue::WebPageEntry *firstEntry = m_queue.at(0);
        return firstEntry
                && firstEntry->webPage
                && firstEntry->webPage->tabId() == tabId;
    }
    return false;
}

DeclarativeWebPage *WebPageQueue::activate(int tabId)
{
    int index = -1;
    WebPageEntry *pageEntry = find(tabId, index);
    // No need to change position for the first index.
    if (index > 0) {
        m_queue.removeAt(index);
        m_queue.prepend(pageEntry);
    }

    return pageEntry ? pageEntry->webPage : 0;
}

DeclarativeWebPage *WebPageQueue::activeWebPage() const
{
    return !m_queue.isEmpty() ? m_queue.at(0)->webPage : 0;
}

void WebPageQueue::release(int tabId, bool virtualize)
{
    int index = -1;
    WebPageEntry *pageEntry = find(tabId, index);
#if DEBUG_LOGS
    qDebug() << "--- beginning: " << tabId << virtualize << pageEntry << (pageEntry ? pageEntry->webPage : 0);
    dumpPages();
#endif
    if (pageEntry) {
        if (pageEntry->webPage) {
            if (virtualize) {
                pageEntry->cssContentRect = new QRectF(pageEntry->webPage->contentRect());
            }
            if (pageEntry->webPage->completed()) {
                pageEntry->webPage->setParent(0);
                delete pageEntry->webPage;
            } else {
                QObject::connect(pageEntry->webPage.data(), &DeclarativeWebPage::completedChanged,
                                 pageEntry->webPage.data(), &QObject::deleteLater);
            }
        }

        pageEntry->webPage = 0;
        if (!virtualize && index >= 0) {
            delete pageEntry;
            m_queue.removeAt(index);
        }
    }

#if DEBUG_LOGS
    qDebug() << "--- end ---";
    dumpPages();
#endif
}


void WebPageQueue::prepend(int tabId, DeclarativeWebPage *webPage)
{
    int index = -1;
    WebPageQueue::WebPageEntry *pageEntry = find(tabId, index);
    if (!pageEntry) {
        pageEntry = new WebPageEntry(webPage, 0);
    } else {
        pageEntry->webPage = webPage;
        pageEntry->tabId = tabId;
        pageEntry->parentId = webPage->parentId();
        pageEntry->uniqueId = webPage->uniqueId();
        pageEntry->webPage->setResurrectedContentRect(*pageEntry->cssContentRect);
        if (pageEntry->cssContentRect) {
            delete pageEntry->cssContentRect;
            pageEntry->cssContentRect = 0;
        }
        m_queue.removeAt(index);
    }

    m_queue.prepend(pageEntry);
    updateLivePages();
    m_livePagePrepended = true;
}

void WebPageQueue::clear()
{
    int count = m_queue.count();
    for (int i = 0; i < count; ++i) {
        WebPageEntry *pageEntry = m_queue.at(i);
        pageEntry->allowPageDelete = true;
        delete pageEntry;
    }
    m_queue.clear();
}

int WebPageQueue::tabId(uint32_t uniqueId) const
{
    for (int i = 0; i < m_queue.count(); ++i) {
        WebPageEntry *pageEntry = m_queue.at(i);
        if (pageEntry && (quint32)pageEntry->uniqueId == uniqueId) {
            return pageEntry->tabId;
        }
    }
    return 0;
}

int WebPageQueue::previouslyUsedTabId() const
{
    if (m_queue.count() > 1) {
        WebPageEntry *pageEntry = m_queue.at(1);
        if (pageEntry) {
            return pageEntry->tabId;
        }
    }
    return 0;
}

bool WebPageQueue::setMaxLivePages(int count)
{
    if (m_maxLiveCount != count && count > 0) {
        m_maxLiveCount = count;
        updateLivePages();
        return true;
    }
    return false;
}

int WebPageQueue::maxLivePages() const
{
    return m_maxLiveCount;
}

bool WebPageQueue::virtualizeInactive()
{
    if (!m_livePagePrepended || m_queue.isEmpty() || !m_queue.at(0)->webPage || !m_queue.at(0)->webPage->completed()) {
        // no need to iterate through the queue if only one page alive or zero live pages
        return false;
    }

    DeclarativeWebPage* livePage = m_queue.at(0)->webPage;

    for (int i = 1; i < m_queue.count(); ++i) {
        DeclarativeWebPage* page = m_queue.at(i)->webPage;
        if (page &&
                (livePage->parentId() != (int)page->uniqueId() || (int)livePage->uniqueId() != page->parentId())) {
            release(m_queue.at(i)->tabId, true);
        }
    }

    m_livePagePrepended = false;
    return true;
}

void WebPageQueue::dumpPages() const
{
    qDebug() << "---- start ----";
    for (int i = 0; i < m_queue.count(); ++i) {
        WebPageEntry *pageEntry = m_queue.at(i);
        qDebug() << "tabId: " << pageEntry->tabId;
        qDebug() << "    page: " << pageEntry->webPage;
        if (pageEntry->webPage) {
            qDebug() << "    page title:" << pageEntry->webPage->title();
            qDebug() << "    page title:" << pageEntry->webPage->url();
        }
        qDebug() << "    cssContentRect:" << pageEntry->cssContentRect;
    }
    qDebug() << "---- end ------";
}

void WebPageQueue::updateLivePages()
{
    if (m_queue.count() > m_maxLiveCount && m_maxLiveCount > 1) {
        for (int i = m_maxLiveCount; i < m_queue.count(); ++i) {
            release(m_queue.at(i)->tabId, true);
        }
    }
}

WebPageQueue::WebPageEntry *WebPageQueue::find(int tabId, int &index) const
{
    int count = m_queue.count();
    for (int i = 0; i < count; ++i) {
        if (m_queue.at(i)->tabId == tabId) {
            index = i;
            return m_queue.at(i);
        }
    }
    index = -1;
    return 0;
}

WebPageQueue::WebPageEntry::WebPageEntry(DeclarativeWebPage *webPage, QRectF *cssContentRect)
    : webPage(webPage)
    , tabId(webPage ? webPage->tabId() : 0)
    , uniqueId(webPage ? webPage->uniqueId() : 0)
    , parentId(webPage ? webPage->parentId() : 0)
    , cssContentRect(cssContentRect)
    , allowPageDelete(false)
{
}

WebPageQueue::WebPageEntry::~WebPageEntry()
{
    if (cssContentRect) {
        delete cssContentRect;
    }

    if (webPage && (webPage->completed() || allowPageDelete)) {
        webPage->setParent(0);
        delete webPage;
    }

    cssContentRect = 0;
    webPage = 0;
}
