#pragma once

// Builder base class
class Builder 
{
public:
    virtual ~Builder() = default;

    virtual bool Build() = 0;
};