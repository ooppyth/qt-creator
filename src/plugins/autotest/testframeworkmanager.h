// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "itestframework.h"

namespace Autotest {

class TestFrameworkManager final
{

public:
    TestFrameworkManager();
    ~TestFrameworkManager();

    bool registerTestFramework(ITestFramework *framework);
    bool registerTestTool(ITestTool *testTool);
    void synchronizeSettings();

    static ITestFramework *frameworkForId(Utils::Id frameworkId);
    static ITestTool *testToolForId(Utils::Id testToolId);
    static ITestTool *testToolForBuildSystemId(Utils::Id buildSystemId);
    static void activateFrameworksAndToolsFromSettings();
    static const TestFrameworks registeredFrameworks();
    static const TestTools registeredTestTools();

private:
    TestFrameworks m_registeredFrameworks;
    TestTools m_registeredTestTools;
};

} // namespace Autotest
